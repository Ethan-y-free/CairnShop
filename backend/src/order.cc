#include "cairnshop/order.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "cairnshop/cart.h"
#include "cairnshop/config_service.h"
#include "cairnshop/crypto.h"
#include "cairnshop/promo.h"

namespace cairnshop
{

namespace
{

constexpr const char* kFlagCheckout = "cairnshop/flags/checkout_enabled";
constexpr const char* kGlobalCheckout = "cairnshop/global/checkout";

class Transaction
{
public:
    explicit Transaction(MYSQL* mysql)
        : mysql_(mysql)
    {
    }

    ~Transaction()
    {
        if (active_)
        {
            Execute(mysql_, "ROLLBACK", nullptr);
        }
    }

    bool Begin(std::string* error)
    {
        if (!Execute(mysql_, "START TRANSACTION", error))
        {
            return false;
        }
        active_ = true;
        return true;
    }

    bool Commit(std::string* error)
    {
        if (!Execute(mysql_, "COMMIT", error))
        {
            return false;
        }
        active_ = false;
        return true;
    }

    void Rollback()
    {
        if (active_)
        {
            Execute(mysql_, "ROLLBACK", nullptr);
            active_ = false;
        }
    }

private:
    MYSQL* mysql_;
    bool active_ = false;
};

std::string ErrorJson(const std::string& code, const std::string& message)
{
    nlohmann::json body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    return body.dump();
}

CheckoutResult ErrorResult(int status,
                           const std::string& code,
                           const std::string& message)
{
    CheckoutResult result;
    result.http_status = status;
    result.body = ErrorJson(code, message);
    return result;
}

std::string UtcTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string UtcOrderSuffix()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &tm);
    return buffer;
}

bool ConfigEnabled(const ConfigSnapshot& snapshot,
                   const char* key,
                   bool default_value)
{
    const auto it = snapshot.values.find(key);
    if (it == snapshot.values.end())
    {
        return default_value;
    }
    nlohmann::json parsed = nlohmann::json::parse(it->second, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
    {
        return default_value;
    }
    const auto enabled = parsed.find("enabled");
    if (enabled == parsed.end() || !enabled->is_boolean())
    {
        return default_value;
    }
    return enabled->get<bool>();
}

OrderView ReadOrderRow(const SqlResult& result, size_t row)
{
    OrderView order;
    order.id = std::stoull(result.At(row, 0));
    order.order_no = result.At(row, 1);
    order.user_id = std::stoull(result.At(row, 2));
    order.status = result.At(row, 3);
    order.subtotal_cents = std::stoll(result.At(row, 4));
    order.discount_cents = std::stoll(result.At(row, 5));
    order.total_cents = std::stoll(result.At(row, 6));
    order.promo_snapshot = result.At(row, 7);
    if (order.promo_snapshot.empty())
    {
        order.promo_snapshot = "{}";
    }
    order.config_version = std::stoull(result.At(row, 8));
    order.created_at = result.At(row, 9);
    return order;
}

const char* kOrderColumns =
    "id, order_no, user_id, status, subtotal_cents, discount_cents, "
    "total_cents, promo_snapshot, config_version, created_at";

bool LoadOrderItems(MYSQL* mysql,
                    uint64_t order_id,
                    std::vector<OrderItemView>* items,
                    std::string* error)
{
    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT product_id, product_name_snapshot, unit_price_cents, quantity "
        "FROM order_items WHERE order_id = " +
            std::to_string(order_id) + " ORDER BY id",
        error);
    if (!result.has_value())
    {
        return false;
    }
    items->clear();
    items->reserve(result->size());
    for (size_t row = 0; row < result->size(); ++row)
    {
        OrderItemView item;
        item.product_id = std::stoull(result->At(row, 0));
        item.product_name = result->At(row, 1);
        item.unit_price_cents = std::stoll(result->At(row, 2));
        item.quantity = std::stoi(result->At(row, 3));
        items->push_back(std::move(item));
    }
    return true;
}

}

nlohmann::json OrderToJson(const OrderView& order)
{
    nlohmann::json body;
    body["id"] = order.id;
    body["order_no"] = order.order_no;
    body["user_id"] = order.user_id;
    body["status"] = order.status;
    body["subtotal_cents"] = order.subtotal_cents;
    body["discount_cents"] = order.discount_cents;
    body["total_cents"] = order.total_cents;
    body["config_version"] = order.config_version;
    body["created_at"] = order.created_at;

    nlohmann::json promo =
        nlohmann::json::parse(order.promo_snapshot, nullptr, false);
    body["promo"] =
        (promo.is_discarded() || !promo.is_object()) ? nlohmann::json::object()
                                                     : promo;

    body["items"] = nlohmann::json::array();
    for (const OrderItemView& item : order.items)
    {
        nlohmann::json entry;
        entry["product_id"] = item.product_id;
        entry["product_name"] = item.product_name;
        entry["unit_price_cents"] = item.unit_price_cents;
        entry["quantity"] = item.quantity;
        body["items"].push_back(entry);
    }
    return body;
}

OrderService::OrderService(MySqlPool* pool, ConfigService* config)
    : pool_(pool), config_(config)
{
}

CheckoutResult OrderService::Checkout(uint64_t user_id,
                                      const std::string& idempotency_key)
{
    if (idempotency_key.empty() || idempotency_key.size() > 128)
    {
        return ErrorResult(400,
                           "invalid_argument",
                           "Idempotency-Key header is required (1-128 chars)");
    }
    if (config_ == nullptr)
    {
        return ErrorResult(500, "config_unavailable", "config service missing");
    }

    const ConfigSnapshot snapshot = config_->Snapshot();
    if (snapshot.stale)
    {
        return ErrorResult(503,
                           "config_stale",
                           "configuration cache is stale; checkout is closed");
    }
    if (!ConfigEnabled(snapshot, kGlobalCheckout, true) ||
        !ConfigEnabled(snapshot, kFlagCheckout, true))
    {
        return ErrorResult(503, "checkout_disabled", "checkout is disabled");
    }

    std::string error;
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(&error);
    if (lease == nullptr)
    {
        return ErrorResult(500, "db_unavailable", error);
    }
    MYSQL* mysql = lease->get();

    Transaction transaction(mysql);
    if (!transaction.Begin(&error))
    {
        return ErrorResult(500, "db_error", error);
    }

    const std::string request_hash = Sha256Hex("cart-checkout");
    uint64_t inserted = 0;
    if (!Execute(mysql,
                 "INSERT IGNORE INTO idempotency_keys "
                 "(user_id, `key`, request_hash, response_status, "
                 "response_body) VALUES (" +
                     std::to_string(user_id) + ", " +
                     SqlString(mysql, idempotency_key) + ", " +
                     SqlString(mysql, request_hash) + ", 0, '{}')",
                 &error,
                 &inserted))
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }

    if (inserted == 0)
    {
        std::optional<SqlResult> existing = Query(
            mysql,
            "SELECT response_status, response_body FROM idempotency_keys "
            "WHERE user_id = " +
                std::to_string(user_id) + " AND `key` = " +
                SqlString(mysql, idempotency_key) + " LIMIT 1",
            &error);
        transaction.Rollback();
        if (!existing.has_value())
        {
            return ErrorResult(500, "db_error", error);
        }
        if (existing->size() == 0)
        {
            return ErrorResult(409, "conflict", "idempotency key conflict");
        }
        const int status = std::stoi(existing->At(0, 0));
        if (status == 0)
        {
            return ErrorResult(409,
                               "conflict",
                               "duplicate request is still in progress");
        }
        CheckoutResult result;
        result.http_status = status;
        result.body = existing->At(0, 1);
        return result;
    }

    uint64_t cart_id = 0;
    if (!GetOrCreateCart(mysql, user_id, &cart_id, &error))
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }
    if (!Execute(mysql,
                 "SELECT id FROM carts WHERE id = " +
                     std::to_string(cart_id) + " FOR UPDATE",
                 &error))
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }

    std::optional<SqlResult> cart = Query(
        mysql,
        "SELECT ci.product_id, p.sku, p.name, p.price_cents, p.stock, "
        "p.status, ci.quantity FROM cart_items ci "
        "JOIN products p ON p.id = ci.product_id "
        "WHERE ci.cart_id = " +
            std::to_string(cart_id) +
            " ORDER BY ci.id FOR UPDATE",
        &error);
    if (!cart.has_value())
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }
    if (cart->size() == 0)
    {
        transaction.Rollback();
        return ErrorResult(409, "cart_empty", "cart is empty");
    }

    int64_t subtotal_cents = 0;
    std::vector<OrderItemView> items;
    items.reserve(cart->size());

    for (size_t row = 0; row < cart->size(); ++row)
    {
        const uint64_t product_id = std::stoull(cart->At(row, 0));
        const std::string sku = cart->At(row, 1);
        const std::string name = cart->At(row, 2);
        const int64_t price_cents = std::stoll(cart->At(row, 3));
        const int64_t stock = std::stoll(cart->At(row, 4));
        const std::string status = cart->At(row, 5);
        const int quantity = std::stoi(cart->At(row, 6));

        if (status != "active" || quantity <= 0 || stock < quantity)
        {
            transaction.Rollback();
            return ErrorResult(409,
                               "out_of_stock",
                               "out of stock: " + sku);
        }

        uint64_t affected = 0;
        if (!Execute(mysql,
                     "UPDATE products SET stock = stock - " +
                         std::to_string(quantity) +
                         " WHERE id = " + std::to_string(product_id) +
                         " AND status = 'active' AND stock >= " +
                         std::to_string(quantity),
                     &error,
                     &affected))
        {
            transaction.Rollback();
            return ErrorResult(500, "db_error", error);
        }
        if (affected != 1)
        {
            transaction.Rollback();
            return ErrorResult(409,
                               "out_of_stock",
                               "out of stock: " + sku);
        }

        subtotal_cents += price_cents * quantity;

        OrderItemView item;
        item.product_id = product_id;
        item.product_name = name;
        item.unit_price_cents = price_cents;
        item.quantity = quantity;
        items.push_back(std::move(item));
    }

    const PromoDecision promo = EvaluatePromo(snapshot, subtotal_cents);
    int64_t discount_cents = promo.discount_cents;
    if (discount_cents > subtotal_cents)
    {
        discount_cents = subtotal_cents;
    }
    const int64_t total_cents = subtotal_cents - discount_cents;

    const std::string order_no = "CS" + UtcOrderSuffix() + RandomHex(3);
    const std::string insert_order =
        "INSERT INTO orders (order_no, user_id, status, subtotal_cents, "
        "discount_cents, total_cents, promo_snapshot, config_version, "
        "created_at, updated_at) VALUES (" +
        SqlString(mysql, order_no) + ", " + std::to_string(user_id) +
        ", 'pending', " + std::to_string(subtotal_cents) + ", " +
        std::to_string(discount_cents) + ", " + std::to_string(total_cents) +
        ", " + SqlString(mysql, promo.snapshot_json) + ", " +
        std::to_string(promo.config_version) + ", NOW(6), NOW(6))";
    if (!Execute(mysql, insert_order, &error))
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }
    const uint64_t order_id = static_cast<uint64_t>(mysql_insert_id(mysql));

    for (const OrderItemView& item : items)
    {
        const std::string insert_item =
            "INSERT INTO order_items (order_id, product_id, "
            "product_name_snapshot, unit_price_cents, quantity) VALUES (" +
            std::to_string(order_id) + ", " +
            std::to_string(item.product_id) + ", " +
            SqlString(mysql, item.product_name) + ", " +
            std::to_string(item.unit_price_cents) + ", " +
            std::to_string(item.quantity) + ")";
        if (!Execute(mysql, insert_item, &error))
        {
            transaction.Rollback();
            return ErrorResult(500, "db_error", error);
        }
    }

    if (!Execute(mysql,
                 "INSERT INTO payments (order_id, status, amount_cents, "
                 "provider, created_at) VALUES (" +
                     std::to_string(order_id) + ", 'pending', " +
                     std::to_string(total_cents) + ", 'mock', NOW(6))",
                 &error))
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }

    if (!Execute(mysql,
                 "DELETE FROM cart_items WHERE cart_id = " +
                     std::to_string(cart_id),
                 &error))
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }

    OrderView view;
    view.id = order_id;
    view.order_no = order_no;
    view.user_id = user_id;
    view.status = "pending";
    view.subtotal_cents = subtotal_cents;
    view.discount_cents = discount_cents;
    view.total_cents = total_cents;
    view.promo_snapshot = promo.snapshot_json;
    view.config_version = promo.config_version;
    view.created_at = UtcTimestamp();
    view.items = items;

    const std::string body = OrderToJson(view).dump();
    if (!Execute(mysql,
                 "UPDATE idempotency_keys SET response_status = 201, "
                 "response_body = " +
                     SqlString(mysql, body) + " WHERE user_id = " +
                     std::to_string(user_id) + " AND `key` = " +
                     SqlString(mysql, idempotency_key),
                 &error))
    {
        transaction.Rollback();
        return ErrorResult(500, "db_error", error);
    }

    if (!transaction.Commit(&error))
    {
        return ErrorResult(500, "db_error", error);
    }

    CheckoutResult result;
    result.http_status = 201;
    result.body = body;
    return result;
}

std::optional<OrderView> OrderService::Get(uint64_t user_id,
                                           const std::string& order_no,
                                           bool is_ops,
                                           std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT " + std::string(kOrderColumns) +
            " FROM orders WHERE order_no = " + SqlString(mysql, order_no) +
            " LIMIT 1",
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }
    if (result->size() == 0)
    {
        *error = "order not found";
        return std::nullopt;
    }

    OrderView order = ReadOrderRow(*result, 0);
    if (!is_ops && order.user_id != user_id)
    {
        *error = "order not found";
        return std::nullopt;
    }
    if (!LoadOrderItems(mysql, order.id, &order.items, error))
    {
        return std::nullopt;
    }
    return order;
}

std::optional<std::vector<OrderView>> OrderService::List(uint64_t user_id,
                                                         int limit,
                                                         int offset,
                                                         std::string* error)
{
    if (limit <= 0)
    {
        limit = 20;
    }
    if (limit > 100)
    {
        limit = 100;
    }
    if (offset < 0)
    {
        offset = 0;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }

    std::optional<SqlResult> result = Query(
        lease->get(),
        "SELECT " + std::string(kOrderColumns) +
            " FROM orders WHERE user_id = " + std::to_string(user_id) +
            " ORDER BY id DESC LIMIT " + std::to_string(offset) + ", " +
            std::to_string(limit),
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }

    std::vector<OrderView> orders;
    orders.reserve(result->size());
    for (size_t row = 0; row < result->size(); ++row)
    {
        orders.push_back(ReadOrderRow(*result, row));
    }
    return orders;
}

bool OrderService::Pay(uint64_t user_id,
                       const std::string& order_no,
                       std::string* error,
                       int* http_status)
{
    *http_status = 500;
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    Transaction transaction(mysql);
    if (!transaction.Begin(error))
    {
        return false;
    }

    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT id, user_id, status, total_cents FROM orders "
        "WHERE order_no = " +
            SqlString(mysql, order_no) + " FOR UPDATE",
        error);
    if (!result.has_value())
    {
        transaction.Rollback();
        return false;
    }
    if (result->size() == 0)
    {
        transaction.Rollback();
        *http_status = 404;
        *error = "order not found";
        return false;
    }

    const uint64_t order_id = std::stoull(result->At(0, 0));
    if (std::stoull(result->At(0, 1)) != user_id)
    {
        transaction.Rollback();
        *http_status = 404;
        *error = "order not found";
        return false;
    }
    if (result->At(0, 2) != "pending")
    {
        transaction.Rollback();
        *http_status = 409;
        *error = "order is not pending";
        return false;
    }

    uint64_t affected = 0;
    if (!Execute(mysql,
                 "UPDATE payments SET status = 'succeeded' WHERE order_id = " +
                     std::to_string(order_id) + " AND status = 'pending'",
                 error,
                 &affected))
    {
        transaction.Rollback();
        return false;
    }
    if (affected == 0)
    {
        transaction.Rollback();
        *http_status = 500;
        *error = "payment record not found";
        return false;
    }

    if (!Execute(mysql,
                 "UPDATE orders SET status = 'paid', updated_at = NOW(6) "
                 "WHERE id = " +
                     std::to_string(order_id),
                 error))
    {
        transaction.Rollback();
        return false;
    }

    if (!transaction.Commit(error))
    {
        return false;
    }
    *http_status = 200;
    return true;
}

bool OrderService::Cancel(uint64_t user_id,
                          const std::string& order_no,
                          std::string* error,
                          int* http_status)
{
    *http_status = 500;
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    Transaction transaction(mysql);
    if (!transaction.Begin(error))
    {
        return false;
    }

    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT id, user_id, status FROM orders WHERE order_no = " +
            SqlString(mysql, order_no) + " FOR UPDATE",
        error);
    if (!result.has_value())
    {
        transaction.Rollback();
        return false;
    }
    if (result->size() == 0)
    {
        transaction.Rollback();
        *http_status = 404;
        *error = "order not found";
        return false;
    }

    const uint64_t order_id = std::stoull(result->At(0, 0));
    if (std::stoull(result->At(0, 1)) != user_id)
    {
        transaction.Rollback();
        *http_status = 404;
        *error = "order not found";
        return false;
    }
    if (result->At(0, 2) != "pending")
    {
        transaction.Rollback();
        *http_status = 409;
        *error = "only pending orders can be cancelled";
        return false;
    }

    std::optional<SqlResult> items = Query(
        mysql,
        "SELECT product_id, quantity FROM order_items WHERE order_id = " +
            std::to_string(order_id),
        error);
    if (!items.has_value())
    {
        transaction.Rollback();
        return false;
    }
    for (size_t row = 0; row < items->size(); ++row)
    {
        const uint64_t product_id = std::stoull(items->At(row, 0));
        const int quantity = std::stoi(items->At(row, 1));
        if (!Execute(mysql,
                     "UPDATE products SET stock = stock + " +
                         std::to_string(quantity) + " WHERE id = " +
                         std::to_string(product_id),
                     error))
        {
            transaction.Rollback();
            return false;
        }
    }

    if (!Execute(mysql,
                 "UPDATE payments SET status = 'failed' WHERE order_id = " +
                     std::to_string(order_id),
                 error))
    {
        transaction.Rollback();
        return false;
    }
    if (!Execute(mysql,
                 "UPDATE orders SET status = 'cancelled', updated_at = NOW(6) "
                 "WHERE id = " +
                     std::to_string(order_id),
                 error))
    {
        transaction.Rollback();
        return false;
    }

    if (!transaction.Commit(error))
    {
        return false;
    }
    *http_status = 200;
    return true;
}

}
