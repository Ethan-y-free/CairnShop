#include "cairnshop/metrics.h"

#include <sstream>

namespace cairnshop
{

void Metrics::OnRequest()
{
    requests_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::OnStatus(int status)
{
    if (status >= 200 && status < 300)
    {
        status_2xx_.fetch_add(1, std::memory_order_relaxed);
    }
    else if (status >= 400 && status < 500)
    {
        status_4xx_.fetch_add(1, std::memory_order_relaxed);
        if (status == 401 || status == 403)
        {
            auth_failures_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    else if (status >= 500)
    {
        status_5xx_.fetch_add(1, std::memory_order_relaxed);
    }
}

void Metrics::OnRateLimited()
{
    rate_limited_.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::OnCheckout(bool ok)
{
    checkouts_.fetch_add(1, std::memory_order_relaxed);
    if (!ok)
    {
        checkout_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::string Metrics::RenderPrometheus() const
{
    std::ostringstream out;
    out << "# HELP cairnshop_http_requests_total HTTP requests received\n"
        << "# TYPE cairnshop_http_requests_total counter\n"
        << "cairnshop_http_requests_total "
        << requests_.load(std::memory_order_relaxed) << "\n"
        << "# HELP cairnshop_http_responses_total Responses by status class\n"
        << "# TYPE cairnshop_http_responses_total counter\n"
        << "cairnshop_http_responses_total{class=\"2xx\"} "
        << status_2xx_.load(std::memory_order_relaxed) << "\n"
        << "cairnshop_http_responses_total{class=\"4xx\"} "
        << status_4xx_.load(std::memory_order_relaxed) << "\n"
        << "cairnshop_http_responses_total{class=\"5xx\"} "
        << status_5xx_.load(std::memory_order_relaxed) << "\n"
        << "# HELP cairnshop_auth_failures_total 401/403 responses\n"
        << "# TYPE cairnshop_auth_failures_total counter\n"
        << "cairnshop_auth_failures_total "
        << auth_failures_.load(std::memory_order_relaxed) << "\n"
        << "# HELP cairnshop_rate_limited_total Requests rejected by rate limits\n"
        << "# TYPE cairnshop_rate_limited_total counter\n"
        << "cairnshop_rate_limited_total "
        << rate_limited_.load(std::memory_order_relaxed) << "\n"
        << "# HELP cairnshop_checkouts_total Checkout attempts\n"
        << "# TYPE cairnshop_checkouts_total counter\n"
        << "cairnshop_checkouts_total "
        << checkouts_.load(std::memory_order_relaxed) << "\n"
        << "# HELP cairnshop_checkout_failures_total Failed checkout attempts\n"
        << "# TYPE cairnshop_checkout_failures_total counter\n"
        << "cairnshop_checkout_failures_total "
        << checkout_failures_.load(std::memory_order_relaxed) << "\n";
    return out.str();
}

}
