#include "cairnshop/promo.h"

#include <algorithm>
#include <ctime>
#include <string>

#include <nlohmann/json.hpp>

namespace cairnshop
{

namespace
{

constexpr const char* kPromoPrefix = "cairnshop/promos/";
constexpr const char* kGlobalCheckout = "cairnshop/global/checkout";

std::string UtcNowIso()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

bool InTimeWindow(const nlohmann::json& promo, const std::string& now)
{
    const auto start = promo.find("start_at");
    if (start != promo.end() && start->is_string() &&
        now < start->get<std::string>())
    {
        return false;
    }
    const auto end = promo.find("end_at");
    if (end != promo.end() && end->is_string() &&
        now > end->get<std::string>())
    {
        return false;
    }
    return true;
}

}

PromoDecision EvaluatePromo(const ConfigSnapshot& config,
                            int64_t subtotal_cents)
{
    PromoDecision decision;

    const auto global = config.values.find(kGlobalCheckout);
    if (global != config.values.end())
    {
        nlohmann::json parsed =
            nlohmann::json::parse(global->second, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object())
        {
            const auto version = parsed.find("version");
            if (version != parsed.end() && version->is_number_unsigned())
            {
                decision.config_version = version->get<uint64_t>();
            }
        }
    }

    const std::string now = UtcNowIso();
    int64_t best_priority = -1;
    int64_t best_discount = 0;

    for (const auto& [key, value] : config.values)
    {
        if (key.rfind(kPromoPrefix, 0) != 0)
        {
            continue;
        }

        nlohmann::json promo = nlohmann::json::parse(value, nullptr, false);
        if (promo.is_discarded() || !promo.is_object())
        {
            continue;
        }

        const auto enabled = promo.find("enabled");
        if (enabled == promo.end() || !enabled->is_boolean() ||
            !enabled->get<bool>())
        {
            continue;
        }

        int64_t threshold = 0;
        const auto threshold_it = promo.find("threshold_cents");
        if (threshold_it != promo.end() && threshold_it->is_number_integer())
        {
            threshold = threshold_it->get<int64_t>();
        }
        if (subtotal_cents < threshold)
        {
            continue;
        }
        if (!InTimeWindow(promo, now))
        {
            continue;
        }

        int64_t priority = 0;
        const auto priority_it = promo.find("priority");
        if (priority_it != promo.end() && priority_it->is_number_integer())
        {
            priority = priority_it->get<int64_t>();
        }

        const auto type_it = promo.find("type");
        if (type_it == promo.end() || !type_it->is_string())
        {
            continue;
        }

        int64_t discount = 0;
        const std::string type = type_it->get<std::string>();
        if (type == "percent")
        {
            int64_t percent = 0;
            const auto percent_it = promo.find("percent");
            if (percent_it == promo.end() ||
                !percent_it->is_number_integer())
            {
                continue;
            }
            percent = percent_it->get<int64_t>();
            if (percent < 0 || percent > 100)
            {
                continue;
            }
            discount = subtotal_cents * percent / 100;
        }
        else if (type == "fixed")
        {
            const auto fixed_it = promo.find("fixed_cents");
            if (fixed_it == promo.end() || !fixed_it->is_number_integer())
            {
                continue;
            }
            discount = fixed_it->get<int64_t>();
        }
        else
        {
            continue;
        }

        if (discount < 0)
        {
            discount = 0;
        }
        if (discount > subtotal_cents)
        {
            discount = subtotal_cents;
        }

        if (priority > best_priority ||
            (priority == best_priority && discount > best_discount))
        {
            best_priority = priority;
            best_discount = discount;
            decision.discount_cents = discount;
            decision.snapshot_json = value;
        }
    }

    return decision;
}

}
