#include <gtest/gtest.h>

#include "cairnshop/promo.h"

namespace cairnshop
{

namespace
{

ConfigSnapshot BaseConfig()
{
    ConfigSnapshot config;
    config.stale = false;
    config.values["cairnshop/global/checkout"] =
        R"({"enabled":true,"version":7})";
    return config;
}

}

TEST(PromoTest, PercentPromotion)
{
    ConfigSnapshot config = BaseConfig();
    config.values["cairnshop/promos/p1"] =
        R"({"enabled":true,"type":"percent","percent":10,)"
        R"("threshold_cents":1000,"priority":10,)"
        R"("start_at":"2020-01-01T00:00:00Z",)"
        R"("end_at":"2030-01-01T00:00:00Z"})";

    const PromoDecision decision = EvaluatePromo(config, 5000);
    EXPECT_EQ(decision.discount_cents, 500);
    EXPECT_EQ(decision.config_version, 7u);
    EXPECT_NE(decision.snapshot_json.find("percent"), std::string::npos);
}

TEST(PromoTest, ThresholdNotMet)
{
    ConfigSnapshot config = BaseConfig();
    config.values["cairnshop/promos/p1"] =
        R"({"enabled":true,"type":"percent","percent":10,)"
        R"("threshold_cents":1000,"priority":10})";

    EXPECT_EQ(EvaluatePromo(config, 999).discount_cents, 0);
    EXPECT_EQ(EvaluatePromo(config, 1000).discount_cents, 100);
}

TEST(PromoTest, DisabledAndExpiredAreIgnored)
{
    ConfigSnapshot config = BaseConfig();
    config.values["cairnshop/promos/disabled"] =
        R"({"enabled":false,"type":"percent","percent":50,)"
        R"("threshold_cents":0,"priority":100})";
    config.values["cairnshop/promos/expired"] =
        R"({"enabled":true,"type":"percent","percent":50,)"
        R"("threshold_cents":0,"priority":100,)"
        R"("end_at":"2000-01-01T00:00:00Z"})";

    EXPECT_EQ(EvaluatePromo(config, 1000).discount_cents, 0);
}

TEST(PromoTest, HighestPriorityWins)
{
    ConfigSnapshot config = BaseConfig();
    config.values["cairnshop/promos/low"] =
        R"({"enabled":true,"type":"percent","percent":10,)"
        R"("threshold_cents":0,"priority":1})";
    config.values["cairnshop/promos/high"] =
        R"({"enabled":true,"type":"percent","percent":20,)"
        R"("threshold_cents":0,"priority":9})";

    EXPECT_EQ(EvaluatePromo(config, 1000).discount_cents, 200);
}

TEST(PromoTest, FixedPromotionIsCappedAtSubtotal)
{
    ConfigSnapshot config = BaseConfig();
    config.values["cairnshop/promos/fixed"] =
        R"({"enabled":true,"type":"fixed","fixed_cents":500,)"
        R"("threshold_cents":0,"priority":1})";

    EXPECT_EQ(EvaluatePromo(config, 300).discount_cents, 300);
}

}
