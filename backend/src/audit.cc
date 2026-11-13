#include "cairnshop/audit.h"

#include <string>
#include <utility>

namespace cairnshop
{

namespace
{

uint64_t ToU64(const std::string& value)
{
    if (value.empty())
    {
        return 0;
    }
    try
    {
        return std::stoull(value);
    }
    catch (...)
    {
        return 0;
    }
}

std::string JsonOrNull(MYSQL* mysql, const std::string& value)
{
    return value.empty() ? "NULL" : SqlString(mysql, value);
}

}

AuditService::AuditService(MySqlPool* pool)
    : pool_(pool)
{
}

bool AuditService::Record(uint64_t actor_id,
                          const std::string& actor_role,
                          const std::string& action,
                          const std::string& target_type,
                          const std::string& target_id,
                          const std::string& before_json,
                          const std::string& after_json,
                          const std::string& request_id,
                          const std::string& ip,
                          std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    const std::string actor =
        actor_id == 0 ? "NULL" : std::to_string(actor_id);
    const std::string sql =
        "INSERT INTO audit_logs (actor_id, actor_role, action, target_type, "
        "target_id, before_json, after_json, request_id, ip, created_at) "
        "VALUES (" +
        actor + ", " + SqlString(mysql, actor_role) + ", " +
        SqlString(mysql, action) + ", " + SqlString(mysql, target_type) + ", " +
        SqlString(mysql, target_id) + ", " + JsonOrNull(mysql, before_json) +
        ", " + JsonOrNull(mysql, after_json) + ", " +
        SqlString(mysql, request_id) + ", " + SqlString(mysql, ip) +
        ", NOW(6))";
    return Execute(mysql, sql, error);
}

std::optional<std::vector<AuditEntry>> AuditService::List(int limit,
                                                          int offset,
                                                          std::string* error)
{
    if (limit <= 0)
    {
        limit = 50;
    }
    if (limit > 200)
    {
        limit = 200;
    }
    if (offset < 0)
    {
        offset = 0;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }

    std::optional<SqlResult> result = Query(
        lease->get(),
        "SELECT id, actor_id, actor_role, action, target_type, target_id, "
        "before_json, after_json, request_id, ip, created_at FROM audit_logs "
        "ORDER BY id DESC LIMIT " +
            std::to_string(offset) + ", " + std::to_string(limit),
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }

    std::vector<AuditEntry> entries;
    entries.reserve(result->size());
    for (size_t row = 0; row < result->size(); ++row)
    {
        AuditEntry entry;
        entry.id = ToU64(result->At(row, 0));
        entry.actor_id = ToU64(result->At(row, 1));
        entry.actor_role = result->At(row, 2);
        entry.action = result->At(row, 3);
        entry.target_type = result->At(row, 4);
        entry.target_id = result->At(row, 5);
        entry.before_json = result->At(row, 6);
        entry.after_json = result->At(row, 7);
        entry.request_id = result->At(row, 8);
        entry.ip = result->At(row, 9);
        entry.created_at = result->At(row, 10);
        entries.push_back(std::move(entry));
    }
    return entries;
}

}
