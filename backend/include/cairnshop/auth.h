#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "cairnshop/mysql.h"

namespace cairnshop
{

struct User
{
    uint64_t id = 0;
    std::string email;
    std::string display_name;
    std::string role;
};

struct Session
{
    std::string token;
    User user;
};

class AuthService
{
public:
    AuthService(MySqlPool* pool, int session_ttl_seconds);

    std::optional<Session> Register(const std::string& email,
                                    const std::string& password,
                                    const std::string& display_name,
                                    std::string* error);
    std::optional<Session> Login(const std::string& email,
                                 const std::string& password,
                                 std::string* error);
    bool Logout(const std::string& token, std::string* error);
    std::optional<User> UserFromToken(const std::string& token,
                                      std::string* error);
    std::optional<User> FindUser(uint64_t id, std::string* error);

private:
    bool CreateSession(MYSQL* mysql,
                       uint64_t user_id,
                       std::string* token,
                       std::string* error);

    MySqlPool* pool_;
    int session_ttl_seconds_;
};

}
