#include "cairnshop/config_admin.h"

#include <ctime>
#include <string>
#include <utility>

#include "cairnshop/crypto.h"
#include "cairnshop/logging.h"

namespace cairnshop
{

namespace
{

constexpr size_t kMaxConfigValueBytes = 60 * 1024;
constexpr size_t kMaxHistory = 20;

std::string UtcNowIso()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

bool IsConfigKey(const std::string& key)
{
    return key.rfind("cairnshop/flags/", 0) == 0 ||
           key.rfind("cairnshop/promos/", 0) == 0 ||
           key.rfind("cairnshop/global/", 0) == 0;
}

class ConfigLock
{
public:
    ConfigLock(MYSQL* mysql, const std::string& key)
        : mysql_(mysql),
          name_("cairnshop:cfg:" + Sha256Hex(key).substr(0, 32))
    {
    }

    ~ConfigLock()
    {
        if (held_)
        {
            Execute(mysql_,
                    "SELECT RELEASE_LOCK(" + SqlString(mysql_, name_) + ")",
                    nullptr);
        }
    }

    bool Acquire(std::string* error)
    {
        std::optional<SqlResult> result = Query(
            mysql_,
            "SELECT GET_LOCK(" + SqlString(mysql_, name_) + ", 2)",
            error);
        if (!result.has_value())
        {
            return false;
        }
        if (result->size() == 0 || result->At(0, 0) != "1")
        {
            *error = "config update is busy";
            return false;
        }
        held_ = true;
        return true;
    }

private:
    MYSQL* mysql_;
    std::string name_;
    bool held_ = false;
};

void AppendHistory(nlohmann::json* history,
                   const nlohmann::json& document,
                   uint64_t version)
{
    nlohmann::json snapshot = document;
    snapshot.erase("history");
    nlohmann::json entry;
    entry["version"] = version;
    entry["value"] = snapshot;
    entry["updated_at"] = document.value("updated_at", std::string());
    entry["updated_by"] = document.value("updated_by", std::string());
    history->push_back(std::move(entry));
    while (history->size() > kMaxHistory)
    {
        history->erase(history->begin());
    }
}

}

ConfigAdminService::ConfigAdminService(MySqlPool* pool,
                                       ConfigService* config,
                                       AuditService* audit)
    : pool_(pool), config_(config), audit_(audit)
{
}

std::optional<nlohmann::json> ConfigAdminService::ReadCurrent(
    const std::string& key,
    bool* found,
    std::string* error,
    int* http_status)
{
    cairnkv::StatusCode code = cairnkv::StatusCode::kOk;
    std::optional<std::pair<std::string, uint64_t>> current =
        config_->GetLatest(key, error, &code);
    if (!current.has_value())
    {
        if (code == cairnkv::StatusCode::kNotFound)
        {
            *found = false;
            return nlohmann::json::object();
        }
        *http_status = 503;
        return std::nullopt;
    }

    nlohmann::json document =
        nlohmann::json::parse(current->first, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        *http_status = 500;
        *error = "stored config is not a JSON object";
        return std::nullopt;
    }
    *found = true;
    return document;
}

std::optional<nlohmann::json> ConfigAdminService::Update(
    uint64_t actor_id,
    const std::string& actor_role,
    const std::string& actor_email,
    const std::string& key,
    const nlohmann::json& value,
    const std::string& request_id,
    const std::string& ip,
    std::string* error,
    int* http_status)
{
    *http_status = 400;
    if (!IsConfigKey(key))
    {
        *error = "only cairnshop/flags, cairnshop/promos and "
                 "cairnshop/global keys can be written";
        return std::nullopt;
    }
    if (!value.is_object())
    {
        *error = "config value must be a JSON object";
        return std::nullopt;
    }
    if (value.dump().size() > kMaxConfigValueBytes)
    {
        *error = "config value is too large";
        return std::nullopt;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        *http_status = 500;
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    ConfigLock lock(mysql, key);
    if (!lock.Acquire(error))
    {
        *http_status = 409;
        return std::nullopt;
    }

    bool found = false;
    std::optional<nlohmann::json> current =
        ReadCurrent(key, &found, error, http_status);
    if (!current.has_value())
    {
        return std::nullopt;
    }

    const uint64_t old_version = current->value("version", 0);
    nlohmann::json history = nlohmann::json::array();
    if (current->contains("history") && (*current)["history"].is_array())
    {
        history = (*current)["history"];
    }
    if (found && old_version > 0)
    {
        AppendHistory(&history, *current, old_version);
    }

    nlohmann::json document = value;
    document["version"] = old_version + 1;
    document["updated_at"] = UtcNowIso();
    document["updated_by"] = actor_email.empty() ? actor_role : actor_email;
    document["history"] = history;

    if (!config_->PutLatest(key, document.dump(), error))
    {
        *http_status = 503;
        return std::nullopt;
    }

    std::string audit_error;
    if (!audit_->Record(actor_id,
                        actor_role,
                        "config.update",
                        "config",
                        key,
                        found ? current->dump() : "{}",
                        document.dump(),
                        request_id,
                        ip,
                        &audit_error))
    {
        LogError("audit_write", audit_error);
    }

    *http_status = 200;
    return document;
}

std::optional<nlohmann::json> ConfigAdminService::Rollback(
    uint64_t actor_id,
    const std::string& actor_role,
    const std::string& actor_email,
    const std::string& key,
    uint64_t target_version,
    const std::string& request_id,
    const std::string& ip,
    std::string* error,
    int* http_status)
{
    *http_status = 400;
    if (!IsConfigKey(key) || target_version == 0)
    {
        *error = "invalid config key or version";
        return std::nullopt;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        *http_status = 500;
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    ConfigLock lock(mysql, key);
    if (!lock.Acquire(error))
    {
        *http_status = 409;
        return std::nullopt;
    }

    bool found = false;
    std::optional<nlohmann::json> current =
        ReadCurrent(key, &found, error, http_status);
    if (!current.has_value())
    {
        return std::nullopt;
    }
    if (!found)
    {
        *http_status = 404;
        *error = "config key not found";
        return std::nullopt;
    }

    const uint64_t current_version = current->value("version", 0);
    if (target_version == current_version)
    {
        *http_status = 409;
        *error = "requested version is already current";
        return std::nullopt;
    }
    if (target_version > current_version)
    {
        *http_status = 404;
        *error = "version not found in history";
        return std::nullopt;
    }

    nlohmann::json history = nlohmann::json::array();
    if (current->contains("history") && (*current)["history"].is_array())
    {
        history = (*current)["history"];
    }

    const nlohmann::json* target = nullptr;
    for (const auto& entry : history)
    {
        if (entry.is_object() && entry.value("version", uint64_t{0}) ==
                                     target_version)
        {
            target = &entry;
            break;
        }
    }
    if (target == nullptr || !target->contains("value") ||
        !(*target)["value"].is_object())
    {
        *http_status = 404;
        *error = "version not found in history";
        return std::nullopt;
    }

    nlohmann::json next_history = history;
    AppendHistory(&next_history, *current, current_version);

    nlohmann::json document = (*target)["value"];
    document["version"] = current_version + 1;
    document["updated_at"] = UtcNowIso();
    document["updated_by"] = actor_email.empty() ? actor_role : actor_email;
    document["history"] = next_history;

    if (!config_->PutLatest(key, document.dump(), error))
    {
        *http_status = 503;
        return std::nullopt;
    }

    std::string audit_error;
    if (!audit_->Record(actor_id,
                        actor_role,
                        "config.rollback",
                        "config",
                        key,
                        current->dump(),
                        document.dump(),
                        request_id,
                        ip,
                        &audit_error))
    {
        LogError("audit_write", audit_error);
    }

    *http_status = 200;
    return document;
}

std::optional<nlohmann::json> ConfigAdminService::History(
    const std::string& key,
    std::string* error,
    int* http_status)
{
    *http_status = 400;
    if (!IsConfigKey(key))
    {
        *error = "invalid config key";
        return std::nullopt;
    }

    bool found = false;
    std::optional<nlohmann::json> current =
        ReadCurrent(key, &found, error, http_status);
    if (!current.has_value())
    {
        return std::nullopt;
    }

    nlohmann::json response;
    response["key"] = key;
    response["current"] = found ? *current : nlohmann::json::object();
    response["history"] =
        (found && current->contains("history") &&
         (*current)["history"].is_array())
            ? (*current)["history"]
            : nlohmann::json::array();
    *http_status = 200;
    return response;
}

}
