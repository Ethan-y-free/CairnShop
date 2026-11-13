#include "cairnshop/cart.h"

#include <string>
#include <utility>

namespace cairnshop
{

namespace
{

constexpr int kMaxQuantity = 99;

}

bool GetOrCreateCart(MYSQL* mysql,
                     uint64_t user_id,
                     uint64_t* cart_id,
                     std::string* error)
{
    std::optional<SqlResult> existing = Query(
        mysql,
        "SELECT id FROM carts WHERE user_id = " + std::to_string(user_id) +
            " LIMIT 1",
        error);
    if (!existing.has_value())
    {
        return false;
    }
    if (existing->size() > 0)
    {
        *cart_id = std::stoull(existing->At(0, 0));
        return true;
    }

    if (!Execute(mysql,
                 "INSERT IGNORE INTO carts (user_id) VALUES (" +
                     std::to_string(user_id) + ")",
                 error))
    {
        return false;
    }

    std::optional<SqlResult> created = Query(
        mysql,
        "SELECT id FROM carts WHERE user_id = " + std::to_string(user_id) +
            " LIMIT 1",
        error);
    if (!created.has_value())
    {
        return false;
    }
    if (created->size() == 0)
    {
        *error = "failed to create cart";
        return false;
    }
    *cart_id = std::stoull(created->At(0, 0));
    return true;
}

CartService::CartService(MySqlPool* pool)
    : pool_(pool)
{
}

std::optional<std::vector<CartItem>> CartService::Get(uint64_t user_id,
                                                      std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }
    MYSQL* mysql = lease->get();

    uint64_t cart_id = 0;
    if (!GetOrCreateCart(mysql, user_id, &cart_id, error))
    {
        return std::nullopt;
    }

    std::optional<SqlResult> result = Query(
        mysql,
        "SELECT ci.product_id, p.sku, p.name, p.image_url, p.price_cents, "
        "p.original_price_cents, p.stock, p.status, ci.quantity "
        "FROM cart_items ci JOIN products p ON p.id = ci.product_id "
        "WHERE ci.cart_id = " +
            std::to_string(cart_id) + " ORDER BY ci.id",
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }

    std::vector<CartItem> items;
    items.reserve(result->size());
    for (size_t row = 0; row < result->size(); ++row)
    {
        CartItem item;
        item.product_id = std::stoull(result->At(row, 0));
        item.sku = result->At(row, 1);
        item.name = result->At(row, 2);
        item.image_url = result->At(row, 3);
        item.price_cents = std::stoll(result->At(row, 4));
        const std::string original = result->At(row, 5);
        item.original_price_cents =
            original.empty() ? 0 : std::stoll(original);
        item.stock = std::stoll(result->At(row, 6));
        item.status = result->At(row, 7);
        item.quantity = std::stoi(result->At(row, 8));
        items.push_back(std::move(item));
    }
    return items;
}

bool CartService::Add(uint64_t user_id,
                      uint64_t product_id,
                      int quantity,
                      std::string* error)
{
    if (quantity <= 0 || quantity > kMaxQuantity)
    {
        *error = "quantity must be between 1 and 99";
        return false;
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    std::optional<SqlResult> product = Query(
        mysql,
        "SELECT stock, status FROM products WHERE id = " +
            std::to_string(product_id) + " LIMIT 1",
        error);
    if (!product.has_value())
    {
        return false;
    }
    if (product->size() == 0)
    {
        *error = "product not found";
        return false;
    }
    const int64_t stock = std::stoll(product->At(0, 0));
    if (product->At(0, 1) != "active")
    {
        *error = "product is not available";
        return false;
    }
    if (stock <= 0)
    {
        *error = "product is out of stock";
        return false;
    }
    const int max_for_product =
        static_cast<int>(stock < kMaxQuantity ? stock : kMaxQuantity);

    uint64_t cart_id = 0;
    if (!GetOrCreateCart(mysql, user_id, &cart_id, error))
    {
        return false;
    }

    const std::string sql =
        "INSERT INTO cart_items (cart_id, product_id, quantity) VALUES (" +
        std::to_string(cart_id) + ", " + std::to_string(product_id) + ", " +
        std::to_string(quantity) +
        ") ON DUPLICATE KEY UPDATE quantity = LEAST(quantity + VALUES(quantity), " +
        std::to_string(max_for_product) + ")";
    return Execute(mysql, sql, error);
}

bool CartService::SetQuantity(uint64_t user_id,
                              uint64_t product_id,
                              int quantity,
                              std::string* error)
{
    if (quantity < 0 || quantity > kMaxQuantity)
    {
        *error = "quantity must be between 0 and 99";
        return false;
    }
    if (quantity == 0)
    {
        return Remove(user_id, product_id, error);
    }

    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    uint64_t cart_id = 0;
    if (!GetOrCreateCart(mysql, user_id, &cart_id, error))
    {
        return false;
    }

    uint64_t affected = 0;
    if (!Execute(mysql,
                 "UPDATE cart_items SET quantity = " + std::to_string(quantity) +
                     " WHERE cart_id = " + std::to_string(cart_id) +
                     " AND product_id = " + std::to_string(product_id),
                 error,
                 &affected))
    {
        return false;
    }
    if (affected == 0)
    {
        *error = "cart item not found";
        return false;
    }
    return true;
}

bool CartService::Remove(uint64_t user_id,
                         uint64_t product_id,
                         std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    uint64_t cart_id = 0;
    if (!GetOrCreateCart(mysql, user_id, &cart_id, error))
    {
        return false;
    }

    return Execute(mysql,
                   "DELETE FROM cart_items WHERE cart_id = " +
                       std::to_string(cart_id) + " AND product_id = " +
                       std::to_string(product_id),
                   error);
}

}
