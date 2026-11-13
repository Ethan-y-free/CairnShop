#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "cairnshop/audit.h"
#include "cairnshop/config_service.h"
#include "cairnshop/mysql.h"

namespace cairnshop
{

// Versioned writes for the CairnKV configuration plane.
//
// Each value is a JSON object managed by the server:
//   { ...business fields..., version, updated_at, updated_by, history[] }
// history keeps up to 20 snapshots of previous documents.
class ConfigAdminService
{
public:
    ConfigAdminService(MySqlPool* pool,
                       ConfigService* config,
                       AuditService* audit);

    std::optional<nlohmann::json> Update(
        uint64_t actor_id,
        const std::string& actor_role,
        const std::string& actor_email,
        const std::string& key,
        const nlohmann::json& value,
        const std::string& request_id,
        const std::string& ip,
        std::string* error,
        int* http_status);

    std::optional<nlohmann::json> Rollback(
        uint64_t actor_id,
        const std::string& actor_role,
        const std::string& actor_email,
        const std::string& key,
        uint64_t target_version,
        const std::string& request_id,
        const std::string& ip,
        std::string* error,
        int* http_status);

    std::optional<nlohmann::json> History(const std::string& key,
                                          std::string* error,
                                          int* http_status);

private:
    std::optional<nlohmann::json> ReadCurrent(const std::string& key,
                                              bool* found,
                                              std::string* error,
                                              int* http_status);

    MySqlPool* pool_;
    ConfigService* config_;
    AuditService* audit_;
};

}
