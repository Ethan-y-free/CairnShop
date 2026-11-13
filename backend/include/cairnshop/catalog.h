#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cairnshop/mysql.h"

namespace cairnshop
{

struct Category
{
    uint64_t id = 0;
    std::string name;
    std::string slug;
};

struct Product
{
    uint64_t id = 0;
    uint64_t category_id = 0;
    std::string sku;
    std::string name;
    std::string description;
    int64_t price_cents = 0;
    int64_t stock = 0;
    std::string status = "draft";
};

class CatalogService
{
public:
    explicit CatalogService(MySqlPool* pool);

    std::optional<std::vector<Category>> ListCategories(std::string* error);
    std::optional<std::vector<Product>> ListProducts(
        std::optional<uint64_t> category_id,
        const std::string& query,
        int limit,
        int offset,
        std::string* error,
        bool only_active = true);
    std::optional<Product> GetProduct(uint64_t id, std::string* error);

    bool CreateCategory(const std::string& name,
                        const std::string& slug,
                        uint64_t* id,
                        std::string* error);
    bool CreateProduct(const Product& product,
                       uint64_t* id,
                       std::string* error);
    bool UpdateProduct(const Product& product, std::string* error);

private:
    MySqlPool* pool_;
};

}
