#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cairnshop/mysql.h"

namespace cairnshop
{

struct CartItem
{
    uint64_t product_id = 0;
    std::string sku;
    std::string name;
    std::string image_url;
    int64_t price_cents = 0;
    int64_t original_price_cents = 0;
    int64_t stock = 0;
    std::string status;
    int quantity = 0;
};

// Returns the user's cart id, creating the cart when necessary. The caller
// owns the MySQL connection and may be inside a transaction.
bool GetOrCreateCart(MYSQL* mysql,
                     uint64_t user_id,
                     uint64_t* cart_id,
                     std::string* error);

class CartService
{
public:
    explicit CartService(MySqlPool* pool);

    std::optional<std::vector<CartItem>> Get(uint64_t user_id,
                                             std::string* error);
    bool Add(uint64_t user_id,
             uint64_t product_id,
             int quantity,
             std::string* error);
    bool SetQuantity(uint64_t user_id,
                     uint64_t product_id,
                     int quantity,
                     std::string* error);
    bool Remove(uint64_t user_id, uint64_t product_id, std::string* error);

private:
    MySqlPool* pool_;
};

}
