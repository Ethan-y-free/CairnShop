#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cairnkv/client.h"

namespace cairnshop
{

using CairnKvAddress = std::pair<std::string, uint16_t>;

struct ConfigSnapshot
{
    std::map<std::string, std::string> values;
    int64_t age_ms = -1;
    bool stale = true;
    bool connected = false;
};

// Reads the CairnShop configuration plane from CairnKV.
//
// The business path uses a cached snapshot refreshed with a leader-local scan
// every refresh_interval_ms. Ops reads use a linearizable Get. If the cache
// has not refreshed for max_stale_ms, Snapshot().stale is true and checkout
// must fail closed.
class ConfigService
{
public:
    ConfigService(std::vector<CairnKvAddress> seeds,
                  int refresh_interval_ms,
                  int max_stale_ms);
    ~ConfigService();

    ConfigService(const ConfigService&) = delete;
    ConfigService& operator=(const ConfigService&) = delete;

    // Starts the background refresh loop. Returns false only on programming
    // errors; an unavailable CairnKV is reported through Ready() and Snapshot().
    bool Start(std::string* error);
    void Stop();

    bool Ready() const;
    ConfigSnapshot Snapshot() const;

    // Linearizable read used by the operations console.
    std::optional<std::pair<std::string, uint64_t>> GetLatest(
        const std::string& key,
        std::string* error);

private:
    bool RefreshOnce(std::string* error);
    bool EnsureClientLocked(std::string* error);
    void Loop();

    std::vector<CairnKvAddress> seeds_;
    size_t next_seed_ = 0;
    int refresh_interval_ms_;
    int max_stale_ms_;

    mutable std::mutex client_mutex_;
    std::unique_ptr<cairnkv::Client> client_;

    mutable std::mutex cache_mutex_;
    std::map<std::string, std::string> cache_;
    std::chrono::steady_clock::time_point last_success_{};
    bool has_success_ = false;

    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// Writes the default flags / promo / global config to CairnKV. Existing keys
// are left untouched. Used by `cairnshop-backend seed-config`.
bool SeedConfig(const std::vector<CairnKvAddress>& seeds, std::string* error);

}
