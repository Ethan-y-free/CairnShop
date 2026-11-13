#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace cairnshop
{

class Metrics
{
public:
    void OnRequest();
    void OnStatus(int status);
    void OnRateLimited();
    void OnCheckout(bool ok);

    std::string RenderPrometheus() const;

private:
    std::atomic<uint64_t> requests_{0};
    std::atomic<uint64_t> status_2xx_{0};
    std::atomic<uint64_t> status_4xx_{0};
    std::atomic<uint64_t> status_5xx_{0};
    std::atomic<uint64_t> auth_failures_{0};
    std::atomic<uint64_t> rate_limited_{0};
    std::atomic<uint64_t> checkouts_{0};
    std::atomic<uint64_t> checkout_failures_{0};
};

}
