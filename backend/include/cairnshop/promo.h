#pragma once

#include <cstdint>
#include <string>

#include "cairnshop/config_service.h"

namespace cairnshop
{

struct PromoDecision
{
    int64_t discount_cents = 0;
    std::string snapshot_json = "{}";
    uint64_t config_version = 0;
};

// Evaluates the enabled promotions in the cached configuration against a
// subtotal. The highest priority eligible promotion wins; ties go to the
// larger discount. Invalid or expired promotions are ignored.
PromoDecision EvaluatePromo(const ConfigSnapshot& config,
                            int64_t subtotal_cents);

}
