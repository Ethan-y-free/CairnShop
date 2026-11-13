#include "cairnshop/auth.h"

#include <utility>

#include "cairnshop/crypto.h"
#include "cairnshop/logging.h"

namespace cairnshop
{

namespace
{

User ReadUser(const SqlResult& result)
{
    User user;
    user.id = std::stoull(result.At(0, 0));
    user.email = result.At(0, 1);
    user.display_name = result.At(0, 2);
    user.role = result.At(0, 3);
    return user;
}

}

AuthService::AuthService(MySqlPool* pool, int session_ttl_seconds)
    : pool_(pool), session_ttl_seconds_(session_ttl_seconds)
{
}

bool AuthService::CreateSession(MYSQL* mysql,
                                uint64_t user_id,
                                std::string* token,
                                std::string* error)
{
    *token = RandomHex(32);
    if (token->empty())
    {
        *error = "failed to generate session token";
        return false;
    }

    const std::string token_hash = Sha256Hex(*token);
    const std::string sql =
        "INSERT INTO sessions (token_hash, user_id, expires_at, created_at, "
        "last_seen_at) VALUES (" +
        SqlString(mysql, token_hash) + ", " + std::to_string(user_id) +
        ", DATE_ADD(NOW(6), INTERVAL " +
        std::to_string(session_ttl_seconds_) +
        " SECOND), NOW(6), NOW(6))";
    return Execute(mysql, sql, error);
}

std::optional<Session> AuthService::Register(const std::string& email,
                                             const std::string& password,
                                             const std::string& display_name,
                                             std::string* error)
{
    if (!IsValidEmail(email))
    {
        *error = "invalid email";
        return std::nullopt;
    }
    if (!IsValidPassword(password))
    {
        *error = "password must be 8-128 characters";
        return std::nullopt;
    }
    if (!IsValidDisplayName(display_name))
    {
        *error = "display_name must be 1-64 characters";
        return std::nullopt;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    std::optional<SqlResult> existing = Query(
        mysql,
        "SELECT id FROM users WHERE email = " + SqlString(mysql, email) +
            " LIMIT 1",
        error);
    if (!existing.has_value())
    {
        return std::nullopt;
    }
    if (existing->size() > 0)
    {
        *error = "email already registered";
        return std::nullopt;
    }

    const std::string password_hash = HashPassword(password);
    if (password_hash.empty())
    {
        *error = "failed to hash password";
        return std::nullopt;
    }

    const std::string sql =
        "INSERT INTO users (email, password_hash, display_name, role) VALUES (" +
        SqlString(mysql, email) + ", " + SqlString(mysql, password_hash) + ", " +
        SqlString(mysql, display_name) + ", 'customer')";
    if (!Execute(mysql, sql, error))
    {
        return std::nullopt;
    }

    Session session;
    session.user.id = static_cast<uint64_t>(mysql_insert_id(mysql));
    session.user.email = email;
    session.user.display_name = display_name;
    session.user.role = "customer";
    if (!CreateSession(mysql, session.user.id, &session.token, error))
    {
        return std::nullopt;
    }
    return session;
}

std::optional<Session> AuthService::Login(const std::string& email,
                                          const std::string& password,
                                          std::string* error)
{
    if (!IsValidEmail(email) || password.empty())
    {
        *error = "invalid email or password";
        return std::nullopt;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT id, email, display_name, role, password_hash FROM users "
        "WHERE email = " +
            SqlString(mysql, email) + " LIMIT 1",
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }
    if (result->size() == 0)
    {
        *error = "invalid email or password";
        return std::nullopt;
    }

    if (!VerifyPassword(password, result->At(0, 4)))
    {
        *error = "invalid email or password";
        return std::nullopt;
    }

    Session session;
    session.user.id = std::stoull(result->At(0, 0));
    session.user.email = result->At(0, 1);
    session.user.display_name = result->At(0, 2);
    session.user.role = result->At(0, 3);
    if (!CreateSession(mysql, session.user.id, &session.token, error))
    {
        return std::nullopt;
    }
    return session;
}

bool AuthService::Logout(const std::string& token, std::string* error)
{
    if (token.empty())
    {
        return true;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    return Execute(lease->get(),
                   "DELETE FROM sessions WHERE token_hash = " +
                       SqlString(lease->get(), Sha256Hex(token)),
                   error);
}

std::optional<User> AuthService::UserFromToken(const std::string& token,
                                               std::string* error)
{
    if (token.empty())
    {
        *error = "missing session token";
        return std::nullopt;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT u.id, u.email, u.display_name, u.role FROM sessions s "
        "JOIN users u ON u.id = s.user_id WHERE s.token_hash = " +
            SqlString(mysql, Sha256Hex(token)) +
            " AND s.expires_at > NOW(6) LIMIT 1",
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }
    if (result->size() == 0)
    {
        *error = "invalid or expired session";
        return std::nullopt;
    }
    return ReadUser(*result);
}

std::optional<User> AuthService::FindUser(uint64_t id, std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT id, email, display_name, role FROM users WHERE id = " +
            std::to_string(id) + " LIMIT 1",
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }
    if (result->size() == 0)
    {
        *error = "user not found";
        return std::nullopt;
    }
    return ReadUser(*result);
}

}
