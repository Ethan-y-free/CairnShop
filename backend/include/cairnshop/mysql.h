#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mysql.h>

#include "cairnshop/config.h"

namespace cairnshop
{

struct SqlResult
{
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;

    size_t size() const
    {
        return rows.size();
    }

    const std::string& At(size_t row, size_t column) const
    {
        return rows.at(row).at(column);
    }
};

class MySqlPool;

class MySqlLease
{
public:
    MySqlLease(MySqlPool* pool, MYSQL* mysql);
    ~MySqlLease();

    MySqlLease(const MySqlLease&) = delete;
    MySqlLease& operator=(const MySqlLease&) = delete;

    MYSQL* get() const
    {
        return mysql_;
    }

private:
    MySqlPool* pool_ = nullptr;
    MYSQL* mysql_ = nullptr;
};

class MySqlPool
{
public:
    explicit MySqlPool(Config config);
    ~MySqlPool();

    MySqlPool(const MySqlPool&) = delete;
    MySqlPool& operator=(const MySqlPool&) = delete;

    bool Initialize(std::string* error);
    std::unique_ptr<MySqlLease> Acquire(std::string* error);
    bool Ping();

private:
    friend class MySqlLease;

    MYSQL* Connect(std::string* error);
    void Release(MYSQL* mysql);

    Config config_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<MYSQL*> free_;
    bool stopping_ = false;
};

bool Execute(MYSQL* mysql,
             const std::string& sql,
             std::string* error,
             uint64_t* affected_rows = nullptr);

std::optional<SqlResult> Query(MYSQL* mysql,
                               const std::string& sql,
                               std::string* error);

std::string Escape(MYSQL* mysql, std::string_view value);

// Quotes and escapes a value for direct use inside a single-quoted SQL literal.
std::string SqlString(MYSQL* mysql, std::string_view value);

}
