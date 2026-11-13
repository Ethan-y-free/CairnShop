#include "cairnshop/catalog.h"

#include <string>
#include <utility>

namespace cairnshop
{

namespace
{

constexpr int kMaxLimit = 100;

Product ReadProduct(const SqlResult& result)
{
    Product product;
    product.id = std::stoull(result.At(0, 0));
    product.category_id = std::stoull(result.At(0, 1));
    product.sku = result.At(0, 2);
    product.name = result.At(0, 3);
    product.description = result.At(0, 4);
    product.price_cents = std::stoll(result.At(0, 5));
    product.stock = std::stoll(result.At(0, 6));
    product.status = result.At(0, 7);
    return product;
}

constexpr const char* kProductColumns =
    "id, category_id, sku, name, description, price_cents, stock, status";

}

CatalogService::CatalogService(MySqlPool* pool)
    : pool_(pool)
{
}

std::optional<std::vector<Category>> CatalogService::ListCategories(
    std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }

    std::optional<SqlResult> result = Query(
        lease->get(),
        "SELECT id, name, slug FROM categories ORDER BY id",
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }

    std::vector<Category> categories;
    categories.reserve(result->size());
    for (size_t row = 0; row < result->size(); ++row)
    {
        Category category;
        category.id = std::stoull(result->At(row, 0));
        category.name = result->At(row, 1);
        category.slug = result->At(row, 2);
        categories.push_back(std::move(category));
    }
    return categories;
}

std::optional<std::vector<Product>> CatalogService::ListProducts(
    std::optional<uint64_t> category_id,
    const std::string& query,
    int limit,
    int offset,
    std::string* error,
    bool only_active)
{
    if (limit <= 0)
    {
        limit = 20;
    }
    if (limit > kMaxLimit)
    {
        limit = kMaxLimit;
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
    MYSQL* mysql = lease->get();

    std::string sql = "SELECT ";
    sql += kProductColumns;
    sql += " FROM products WHERE 1 = 1";
    if (only_active)
    {
        sql += " AND status = 'active'";
    }
    if (category_id.has_value())
    {
        sql += " AND category_id = " + std::to_string(*category_id);
    }
    if (!query.empty())
    {
        sql += " AND name LIKE " + SqlString(mysql, "%" + query + "%");
    }
    sql += " ORDER BY id LIMIT " + std::to_string(offset) + ", " +
           std::to_string(limit);

    std::optional<SqlResult> result = Query(mysql, sql, error);
    if (!result.has_value())
    {
        return std::nullopt;
    }

    std::vector<Product> products;
    products.reserve(result->size());
    for (size_t row = 0; row < result->size(); ++row)
    {
        Product product;
        product.id = std::stoull(result->At(row, 0));
        product.category_id = std::stoull(result->At(row, 1));
        product.sku = result->At(row, 2);
        product.name = result->At(row, 3);
        product.description = result->At(row, 4);
        product.price_cents = std::stoll(result->At(row, 5));
        product.stock = std::stoll(result->At(row, 6));
        product.status = result->At(row, 7);
        products.push_back(std::move(product));
    }
    return products;
}

std::optional<Product> CatalogService::GetProduct(uint64_t id,
                                                  std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return std::nullopt;
    }

    std::optional<SqlResult> result = Query(
        lease->get(),
        "SELECT " + std::string(kProductColumns) +
            " FROM products WHERE id = " + std::to_string(id) + " LIMIT 1",
        error);
    if (!result.has_value())
    {
        return std::nullopt;
    }
    if (result->size() == 0)
    {
        *error = "product not found";
        return std::nullopt;
    }
    return ReadProduct(*result);
}

bool CatalogService::CreateCategory(const std::string& name,
                                    const std::string& slug,
                                    uint64_t* id,
                                    std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    const std::string sql = "INSERT INTO categories (name, slug) VALUES (" +
                            SqlString(mysql, name) + ", " +
                            SqlString(mysql, slug) + ")";
    if (!Execute(mysql, sql, error))
    {
        return false;
    }
    if (id != nullptr)
    {
        *id = static_cast<uint64_t>(mysql_insert_id(mysql));
    }
    return true;
}

bool CatalogService::CreateProduct(const Product& product,
                                   uint64_t* id,
                                   std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    const std::string sql =
        "INSERT INTO products (category_id, sku, name, description, "
        "price_cents, stock, status, created_at, updated_at) VALUES (" +
        std::to_string(product.category_id) + ", " +
        SqlString(mysql, product.sku) + ", " + SqlString(mysql, product.name) +
        ", " + SqlString(mysql, product.description) + ", " +
        std::to_string(product.price_cents) + ", " +
        std::to_string(product.stock) + ", " + SqlString(mysql, product.status) +
        ", NOW(6), NOW(6))";
    if (!Execute(mysql, sql, error))
    {
        return false;
    }
    if (id != nullptr)
    {
        *id = static_cast<uint64_t>(mysql_insert_id(mysql));
    }
    return true;
}

bool CatalogService::UpdateProduct(const Product& product, std::string* error)
{
    std::unique_ptr<MySqlLease> lease = pool_->Acquire(error);
    if (lease == nullptr)
    {
        return false;
    }
    MYSQL* mysql = lease->get();

    const std::string sql =
        "UPDATE products SET category_id = " +
        std::to_string(product.category_id) + ", sku = " +
        SqlString(mysql, product.sku) + ", name = " +
        SqlString(mysql, product.name) + ", description = " +
        SqlString(mysql, product.description) + ", price_cents = " +
        std::to_string(product.price_cents) + ", stock = " +
        std::to_string(product.stock) + ", status = " +
        SqlString(mysql, product.status) +
        ", updated_at = NOW(6) WHERE id = " + std::to_string(product.id);
    return Execute(mysql, sql, error);
}

}
