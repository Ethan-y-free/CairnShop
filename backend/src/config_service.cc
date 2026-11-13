#include "cairnshop/config_service.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "cairnkv/status.h"

namespace cairnshop
{

namespace
{

constexpr const char* kConfigPrefix = "cairnshop/";
constexpr uint32_t kScanLimit = 1000;

struct SeedEntry
{
    const char* key;
    const char* value;
};

const SeedEntry kSeedEntries[] = {
    {"cairnshop/meta/schema", R"({"version":1})"},
    {"cairnshop/global/checkout",
     R"({"enabled":true,"message":"","version":1})"},
    {"cairnshop/flags/checkout_enabled", R"({"enabled":true,"version":1})"},
    {"cairnshop/promos/welcome10",
     R"({"id":"welcome10","name":"Welcome 10%","type":"percent",)"
     R"("threshold_cents":0,"percent":10,)"
     R"("start_at":"2026-01-01T00:00:00Z",)"
     R"("end_at":"2027-12-31T23:59:59Z",)"
     R"("enabled":true,"priority":10,"version":1})"},
};

}

ConfigService::ConfigService(std::vector<CairnKvAddress> seeds,
                             int refresh_interval_ms,
                             int max_stale_ms)
    : seeds_(std::move(seeds)),
      refresh_interval_ms_(refresh_interval_ms < 100 ? 100
                                                     : refresh_interval_ms),
      max_stale_ms_(max_stale_ms < 1000 ? 1000 : max_stale_ms)
{
    if (seeds_.empty())
    {
        seeds_.emplace_back("127.0.0.1", 6666);
    }
}

ConfigService::~ConfigService()
{
    Stop();
}

bool ConfigService::EnsureClientLocked(std::string* error)
{
    if (client_ != nullptr)
    {
        return true;
    }

    std::string last_error = "no CairnKV seeds configured";
    for (size_t attempt = 0; attempt < seeds_.size(); ++attempt)
    {
        const size_t index = (next_seed_ + attempt) % seeds_.size();
        cairnkv::ClientOptions options;
        options.host = seeds_[index].first;
        options.port = seeds_[index].second;
        options.timeout_ms = 1000;

        cairnkv::StatusOr<std::unique_ptr<cairnkv::Client>> opened =
            cairnkv::Client::Open(options);
        if (opened.ok())
        {
            client_ = std::move(*opened);
            next_seed_ = index;
            return true;
        }
        last_error = opened.status().ToString();
    }

    if (error != nullptr)
    {
        *error = last_error;
    }
    return false;
}

bool ConfigService::RefreshOnce(std::string* error)
{
    std::lock_guard<std::mutex> client_lock(client_mutex_);
    if (!EnsureClientLocked(error))
    {
        connected_.store(false);
        return false;
    }

    cairnkv::ScanOptions options;
    options.limit = kScanLimit;
    cairnkv::StatusOr<std::vector<cairnkv::KeyValue>> values =
        client_->Scan(kConfigPrefix, options);
    if (!values.ok())
    {
        if (error != nullptr)
        {
            *error = values.status().ToString();
        }
        client_.reset();
        next_seed_ = (next_seed_ + 1) % seeds_.size();
        connected_.store(false);
        return false;
    }

    std::map<std::string, std::string> next;
    for (const cairnkv::KeyValue& value : *values)
    {
        next[value.key] = value.value;
    }

    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        cache_.swap(next);
        last_success_ = std::chrono::steady_clock::now();
        has_success_ = true;
    }
    connected_.store(true);
    return true;
}

bool ConfigService::Start(std::string* error)
{
    if (running_.load())
    {
        return true;
    }

    running_.store(true);
    RefreshOnce(error);
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void ConfigService::Stop()
{
    running_.store(false);
    if (thread_.joinable())
    {
        thread_.join();
    }

    std::lock_guard<std::mutex> client_lock(client_mutex_);
    client_.reset();
    connected_.store(false);
}

void ConfigService::Loop()
{
    while (running_.load())
    {
        std::string error;
        RefreshOnce(&error);
        std::this_thread::sleep_for(
            std::chrono::milliseconds(refresh_interval_ms_));
    }
}

bool ConfigService::Ready() const
{
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    if (!has_success_)
    {
        return false;
    }
    const auto age = std::chrono::steady_clock::now() - last_success_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(age).count() <=
           max_stale_ms_;
}

ConfigSnapshot ConfigService::Snapshot() const
{
    ConfigSnapshot snapshot;
    {
        std::lock_guard<std::mutex> cache_lock(cache_mutex_);
        snapshot.values = cache_;
        if (has_success_)
        {
            const auto age = std::chrono::steady_clock::now() - last_success_;
            snapshot.age_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(age)
                    .count();
            snapshot.stale = snapshot.age_ms > max_stale_ms_;
        }
    }
    snapshot.connected = connected_.load();
    return snapshot;
}

bool ConfigService::PutLatest(const std::string& key,
                              const std::string& value,
                              std::string* error)
{
    std::lock_guard<std::mutex> client_lock(client_mutex_);
    if (!EnsureClientLocked(error))
    {
        return false;
    }

    const cairnkv::Status status = client_->Put(key, value);
    if (!status.ok())
    {
        if (error != nullptr)
        {
            *error = status.ToString();
        }
        client_.reset();
        next_seed_ = (next_seed_ + 1) % seeds_.size();
        return false;
    }
    return true;
}

std::optional<cairnkv::ClusterStatus> ConfigService::GetClusterStatus(
    std::string* error)
{
    std::lock_guard<std::mutex> client_lock(client_mutex_);
    if (!EnsureClientLocked(error))
    {
        return std::nullopt;
    }

    cairnkv::StatusOr<cairnkv::ClusterStatus> status =
        client_->GetStatus();
    if (!status.ok())
    {
        if (error != nullptr)
        {
            *error = status.status().ToString();
        }
        client_.reset();
        next_seed_ = (next_seed_ + 1) % seeds_.size();
        return std::nullopt;
    }
    return *status;
}

std::optional<std::pair<std::string, uint64_t>> ConfigService::GetLatest(
    const std::string& key,
    std::string* error,
    cairnkv::StatusCode* status_code)
{
    std::lock_guard<std::mutex> client_lock(client_mutex_);
    if (!EnsureClientLocked(error))
    {
        if (status_code != nullptr)
        {
            *status_code = cairnkv::StatusCode::kUnavailable;
        }
        return std::nullopt;
    }

    cairnkv::ReadOptions options;
    options.mode = cairnkv::ReadMode::kLinearizable;
    cairnkv::StatusOr<cairnkv::GetResult> result = client_->Get(key, options);
    if (!result.ok())
    {
        if (error != nullptr)
        {
            *error = result.status().ToString();
        }
        if (status_code != nullptr)
        {
            *status_code = result.status().code();
        }
        if (result.status().code() != cairnkv::StatusCode::kNotFound)
        {
            client_.reset();
            next_seed_ = (next_seed_ + 1) % seeds_.size();
        }
        return std::nullopt;
    }
    if (status_code != nullptr)
    {
        *status_code = cairnkv::StatusCode::kOk;
    }
    return std::make_pair(result->value, result->applied_index);
}

bool SeedConfig(const std::vector<CairnKvAddress>& seeds, std::string* error)
{
    std::string last_error = "no CairnKV seeds configured";
    for (const CairnKvAddress& seed : seeds)
    {
        cairnkv::ClientOptions options;
        options.host = seed.first;
        options.port = seed.second;
        options.timeout_ms = 2000;

        cairnkv::StatusOr<std::unique_ptr<cairnkv::Client>> opened =
            cairnkv::Client::Open(options);
        if (!opened.ok())
        {
            last_error = opened.status().ToString();
            continue;
        }
        std::unique_ptr<cairnkv::Client> client = std::move(*opened);

        bool failed = false;
        for (const SeedEntry& entry : kSeedEntries)
        {
            cairnkv::StatusOr<cairnkv::GetResult> existing =
                client->Get(entry.key);
            if (existing.ok())
            {
                continue;
            }
            if (existing.status().code() != cairnkv::StatusCode::kNotFound)
            {
                last_error = existing.status().ToString();
                failed = true;
                break;
            }

            const cairnkv::Status status =
                client->Put(entry.key, entry.value);
            if (!status.ok())
            {
                last_error = status.ToString();
                failed = true;
                break;
            }
        }
        if (!failed)
        {
            return true;
        }
    }

    if (error != nullptr)
    {
        *error = last_error;
    }
    return false;
}

}
