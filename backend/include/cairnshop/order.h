#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cairnshop/mysql.h"

namespace cairnshop
{

class ConfigService;

struct OrderItemView
{
    uint64_t product_id = 0;
    std::string product_name;
    int64_t unit_price_cents = 0;
    int quantity = 0;
};

struct OrderView
{
    uint64_t id = 0;
    std::string order_no;
    uint64_t user_id = 0;
    std::string status;
    int64_t subtotal_cents = 0;
    int64_t discount_cents = 0;
    int64_t total_cents = 0;
    std::string promo_snapshot = "{}";
    uint64_t config_version = 0;
    std::string created_at;
    std::vector<OrderItemView> items;
};

struct CheckoutResult
{
    int http_status = 500;
    std::string body;
};

nlohmann::json OrderToJson(const OrderView& order);

class OrderService
{
public:
    OrderService(MySqlPool* pool, ConfigService* config);

    // Creates an order from the user's cart. The Idempotency-Key contract is
    // enforced inside the same MySQL transaction as stock and order writes.
    CheckoutResult Checkout(uint64_t user_id,
                            const std::string& idempotency_key);

    std::optional<OrderView> Get(uint64_t user_id,
                                 const std::string& order_no,
                                 bool is_ops,
                                 std::string* error);
    std::optional<std::vector<OrderView>> List(uint64_t user_id,
                                               int limit,
                                               int offset,
                                               std::string* error);

    bool Pay(uint64_t user_id,
             const std::string& order_no,
             std::string* error,
             int* http_status);
    bool Cancel(uint64_t user_id,
                const std::string& order_no,
                std::string* error,
                int* http_status);

private:
    MySqlPool* pool_;
    ConfigService* config_;
};

}
