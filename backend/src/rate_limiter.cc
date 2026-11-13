#include "cairnshop/rate_limiter.h"

#include <chrono>

namespace cairnshop
{

bool RateLimiter::Allow(const std::string& key,
                        int limit,
                        int window_seconds)
{
    if (limit <= 0 || window_seconds <= 0)
    {
        return true;
    }

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();

    std::lock_guard<std::mutex> lock(mutex_);

    // Opportunistic cleanup so a long-running process does not accumulate
    // one entry per IP forever.
    ++calls_;
    if (calls_ % 1024 == 0)
    {
        for (auto it = windows_.begin(); it != windows_.end();)
        {
            if (now - it->second.start_seconds >
                static_cast<int64_t>(window_seconds) * 2)
            {
                it = windows_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    auto it = windows_.find(key);
    if (it == windows_.end() ||
        now - it->second.start_seconds >= window_seconds)
    {
        windows_[key] = Window{now, 1};
        return true;
    }
    if (it->second.count >= limit)
    {
        return false;
    }
    ++it->second.count;
    return true;
}

}
