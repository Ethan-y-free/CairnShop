#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cairnshop/mysql.h"

namespace cairnshop
{

struct AuditEntry
{
    uint64_t id = 0;
    uint64_t actor_id = 0;
    std::string actor_role;
    std::string action;
    std::string target_type;
    std::string target_id;
    std::string before_json;
    std::string after_json;
    std::string request_id;
    std::string ip;
    std::string created_at;
};

class AuditService
{
public:
    explicit AuditService(MySqlPool* pool);

    bool Record(uint64_t actor_id,
                const std::string& actor_role,
                const std::string& action,
                const std::string& target_type,
                const std::string& target_id,
                const std::string& before_json,
                const std::string& after_json,
                const std::string& request_id,
                const std::string& ip,
                std::string* error);

    std::optional<std::vector<AuditEntry>> List(int limit,
                                                int offset,
                                                std::string* error);

private:
    MySqlPool* pool_;
};

}
