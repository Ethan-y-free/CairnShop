#include "cairnshop/mysql.h"

#include <chrono>
#include <cstring>
#include <utility>

namespace cairnshop
{

MySqlLease::MySqlLease(MySqlPool* pool, MYSQL* mysql)
    : pool_(pool), mysql_(mysql)
{
}

MySqlLease::~MySqlLease()
{
    if (pool_ != nullptr && mysql_ != nullptr)
    {
        pool_->Release(mysql_);
    }
}

MySqlPool::MySqlPool(Config config)
    : config_(std::move(config))
{
}

MySqlPool::~MySqlPool()
{
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    for (MYSQL* mysql : free_)
    {
        mysql_close(mysql);
    }
    free_.clear();
}

MYSQL* MySqlPool::Connect(std::string* error)
{
    MYSQL* mysql = mysql_init(nullptr);
    if (mysql == nullptr)
    {
        if (error != nullptr)
        {
            *error = "mysql_init failed";
        }
        return nullptr;
    }

    mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    unsigned int timeout = 5;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (mysql_real_connect(mysql,
                           config_.mysql_host.c_str(),
                           config_.mysql_user.c_str(),
                           config_.mysql_password.c_str(),
                           config_.mysql_database.c_str(),
                           static_cast<unsigned int>(config_.mysql_port),
                           nullptr,
                           CLIENT_MULTI_STATEMENTS) == nullptr)
    {
        if (error != nullptr)
        {
            *error = mysql_error(mysql);
        }
        mysql_close(mysql);
        return nullptr;
    }

    return mysql;
}

bool MySqlPool::Initialize(std::string* error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < config_.mysql_pool_size; ++i)
    {
        MYSQL* mysql = Connect(error);
        if (mysql == nullptr)
        {
            for (MYSQL* existing : free_)
            {
                mysql_close(existing);
            }
            free_.clear();
            return false;
        }
        free_.push_back(mysql);
    }
    return true;
}

std::unique_ptr<MySqlLease> MySqlPool::Acquire(std::string* error)
{
    mysql_thread_init();

    std::unique_lock<std::mutex> lock(mutex_);
    if (free_.empty())
    {
        cv_.wait_for(lock,
                     std::chrono::seconds(5),
                     [this] { return stopping_ || !free_.empty(); });
    }
    if (stopping_)
    {
        if (error != nullptr)
        {
            *error = "mysql pool is stopping";
        }
        return nullptr;
    }
    if (free_.empty())
    {
        if (error != nullptr)
        {
            *error = "mysql pool exhausted";
        }
        return nullptr;
    }

    MYSQL* mysql = free_.back();
    free_.pop_back();

    if (mysql_ping(mysql) != 0)
    {
        mysql_close(mysql);
        mysql = Connect(error);
        if (mysql == nullptr)
        {
            return nullptr;
        }
    }

    return std::unique_ptr<MySqlLease>(new MySqlLease(this, mysql));
}

void MySqlPool::Release(MYSQL* mysql)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
        {
            mysql_close(mysql);
            mysql_thread_end();
            return;
        }
        free_.push_back(mysql);
    }
    cv_.notify_one();
}

bool MySqlPool::Ping()
{
    std::string error;
    std::unique_ptr<MySqlLease> lease = Acquire(&error);
    if (lease == nullptr)
    {
        return false;
    }
    return mysql_ping(lease->get()) == 0;
}

bool Execute(MYSQL* mysql,
             const std::string& sql,
             std::string* error,
             uint64_t* affected_rows)
{
    if (mysql_real_query(mysql, sql.c_str(), sql.size()) != 0)
    {
        if (error != nullptr)
        {
            *error = mysql_error(mysql);
        }
        return false;
    }

    if (affected_rows != nullptr)
    {
        *affected_rows = static_cast<uint64_t>(mysql_affected_rows(mysql));
    }

    MYSQL_RES* first = mysql_store_result(mysql);
    if (first != nullptr)
    {
        mysql_free_result(first);
    }

    int next_status = 0;
    while ((next_status = mysql_next_result(mysql)) == 0)
    {
        MYSQL_RES* result = mysql_store_result(mysql);
        if (result != nullptr)
        {
            mysql_free_result(result);
        }
    }
    if (next_status > 0)
    {
        if (error != nullptr)
        {
            *error = mysql_error(mysql);
        }
        return false;
    }
    return true;
}

std::optional<SqlResult> Query(MYSQL* mysql,
                               const std::string& sql,
                               std::string* error)
{
    if (mysql_real_query(mysql, sql.c_str(), sql.size()) != 0)
    {
        if (error != nullptr)
        {
            *error = mysql_error(mysql);
        }
        return std::nullopt;
    }

    MYSQL_RES* result = mysql_store_result(mysql);
    if (result == nullptr)
    {
        if (mysql_field_count(mysql) == 0)
        {
            return SqlResult{};
        }
        if (error != nullptr)
        {
            *error = mysql_error(mysql);
        }
        return std::nullopt;
    }

    SqlResult out;
    const unsigned int column_count = mysql_num_fields(result);
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    for (unsigned int i = 0; i < column_count; ++i)
    {
        out.columns.emplace_back(fields[i].name);
    }

    MYSQL_ROW row = nullptr;
    while ((row = mysql_fetch_row(result)) != nullptr)
    {
        const unsigned long* lengths = mysql_fetch_lengths(result);
        std::vector<std::string> values;
        values.reserve(column_count);
        for (unsigned int i = 0; i < column_count; ++i)
        {
            if (row[i] == nullptr)
            {
                values.emplace_back();
            }
            else
            {
                values.emplace_back(row[i], lengths[i]);
            }
        }
        out.rows.push_back(std::move(values));
    }

    mysql_free_result(result);
    return out;
}

std::string Escape(MYSQL* mysql, std::string_view value)
{
    std::string out(value.size() * 2 + 1, '\0');
    const unsigned long written = mysql_real_escape_string(
        mysql,
        out.data(),
        value.data(),
        static_cast<unsigned long>(value.size()));
    out.resize(written);
    return out;
}

std::string SqlString(MYSQL* mysql, std::string_view value)
{
    return "'" + Escape(mysql, value) + "'";
}

}
