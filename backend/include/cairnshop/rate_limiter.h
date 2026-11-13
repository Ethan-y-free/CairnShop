#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cairnshop
{

// Fixed-window in-memory limiter. One process, one window per key; enough for
// the single-instance v1.0 deployment. A shared limiter is W9+ work.
class RateLimiter
{
public:
    bool Allow(const std::string& key, int limit, int window_seconds);

private:
    struct Window
    {
        int64_t start_seconds = 0;
        int count = 0;
    };

    std::mutex mutex_;
    std::unordered_map<std::string, Window> windows_;
    uint64_t calls_ = 0;
};

}
