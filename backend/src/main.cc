#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <mysql.h>

#include "httplib.h"
#include "nlohmann/json.hpp"

#include "cairnshop/audit.h"
#include "cairnshop/auth.h"
#include "cairnshop/cart.h"
#include "cairnshop/catalog.h"
#include "cairnshop/config.h"
#include "cairnshop/config_admin.h"
#include "cairnshop/config_service.h"
#include "cairnshop/crypto.h"
#include "cairnshop/metrics.h"
#include "cairnshop/order.h"
#include "cairnshop/rate_limiter.h"
#include "cairnshop/logging.h"
#include "cairnshop/mysql.h"

namespace cairnshop
{

namespace
{

constexpr const char* kSessionCookie = "cairnshop_session";

thread_local std::string t_request_id;
thread_local std::chrono::steady_clock::time_point t_request_start;

nlohmann::json UserJson(const User& user)
{
    nlohmann::json body;
    body["id"] = user.id;
    body["email"] = user.email;
    body["display_name"] = user.display_name;
    body["role"] = user.role;
    return body;
}

nlohmann::json CategoryJson(const Category& category)
{
    nlohmann::json body;
    body["id"] = category.id;
    body["name"] = category.name;
    body["slug"] = category.slug;
    return body;
}

nlohmann::json ProductJson(const Product& product)
{
    nlohmann::json body;
    body["id"] = product.id;
    body["category_id"] = product.category_id;
    body["sku"] = product.sku;
    body["name"] = product.name;
    body["description"] = product.description;
    body["price_cents"] = product.price_cents;
    body["stock"] = product.stock;
    body["status"] = product.status;
    return body;
}

nlohmann::json CartItemJson(const CartItem& item)
{
    nlohmann::json body;
    body["product_id"] = item.product_id;
    body["sku"] = item.sku;
    body["name"] = item.name;
    body["price_cents"] = item.price_cents;
    body["stock"] = item.stock;
    body["status"] = item.status;
    body["quantity"] = item.quantity;
    return body;
}

nlohmann::json CartJson(const std::vector<CartItem>& items)
{
    nlohmann::json body;
    body["items"] = nlohmann::json::array();
    int64_t subtotal_cents = 0;
    for (const CartItem& item : items)
    {
        body["items"].push_back(CartItemJson(item));
        subtotal_cents += item.price_cents * item.quantity;
    }
    body["subtotal_cents"] = subtotal_cents;
    return body;
}

void JsonResponse(httplib::Response& res,
                  int status,
                  const nlohmann::json& body)
{
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void ErrorResponse(httplib::Response& res,
                   int status,
                   const std::string& code,
                   const std::string& message)
{
    nlohmann::json body;
    body["error"]["code"] = code;
    body["error"]["message"] = message;
    JsonResponse(res, status, body);
}

bool CheckRate(RateLimiter& limiter,
               Metrics& metrics,
               const std::string& key,
               int limit,
               httplib::Response& res)
{
    if (limiter.Allow(key, limit, 60))
    {
        return true;
    }
    metrics.OnRateLimited();
    res.set_header("Retry-After", "60");
    ErrorResponse(res, 429, "rate_limited", "too many requests");
    return false;
}

std::optional<nlohmann::json> ParseBody(const httplib::Request& req,
                                        httplib::Response& res)
{
    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object())
    {
        ErrorResponse(res, 400, "invalid_json", "request body must be a JSON object");
        return std::nullopt;
    }
    return body;
}

bool GetInt64(const nlohmann::json& body, const char* key, int64_t* out)
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_number_integer())
    {
        return false;
    }
    *out = it->get<int64_t>();
    return true;
}

std::string GetString(const nlohmann::json& body,
                      const char* key,
                      const std::string& fallback = "")
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string())
    {
        return fallback;
    }
    return it->get<std::string>();
}

std::string RequestId(const httplib::Request& req)
{
    const std::string id = req.get_header_value("X-Request-Id");
    return id.empty() ? RandomHex(8) : id.substr(0, 64);
}

std::string ClientIp(const httplib::Request& req)
{
    const std::string forwarded = req.get_header_value("X-Forwarded-For");
    if (!forwarded.empty())
    {
        const size_t comma = forwarded.find(',');
        return comma == std::string::npos ? forwarded
                                          : forwarded.substr(0, comma);
    }
    return req.remote_addr;
}

std::string GetCookie(const httplib::Request& req, const std::string& name)
{
    const auto it = req.headers.find("Cookie");
    if (it == req.headers.end())
    {
        return "";
    }

    const std::string& cookies = it->second;
    size_t pos = 0;
    while (pos < cookies.size())
    {
        const size_t end = cookies.find(';', pos);
        std::string part = cookies.substr(pos, end - pos);
        while (!part.empty() && part.front() == ' ')
        {
            part.erase(part.begin());
        }
        const size_t eq = part.find('=');
        if (eq != std::string::npos)
        {
            const std::string key = part.substr(0, eq);
            if (key == name)
            {
                return part.substr(eq + 1);
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        pos = end + 1;
    }
    return "";
}

void SetSessionCookie(httplib::Response& res,
                      const std::string& token,
                      int ttl_seconds,
                      bool secure)
{
    std::string cookie = std::string(kSessionCookie) + "=" + token +
                         "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" +
                         std::to_string(ttl_seconds);
    if (secure)
    {
        cookie += "; Secure";
    }
    res.set_header("Set-Cookie", cookie);
}

void ClearSessionCookie(httplib::Response& res, bool secure)
{
    std::string cookie = std::string(kSessionCookie) +
                         "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0";
    if (secure)
    {
        cookie += "; Secure";
    }
    res.set_header("Set-Cookie", cookie);
}

std::optional<User> RequireUser(const httplib::Request& req,
                                httplib::Response& res,
                                AuthService& auth,
                                const std::string& required_role = "")
{
    const std::string token = GetCookie(req, kSessionCookie);
    if (token.empty())
    {
        ErrorResponse(res, 401, "unauthorized", "login required");
        return std::nullopt;
    }

    std::string error;
    std::optional<User> user = auth.UserFromToken(token, &error);
    if (!user.has_value())
    {
        ErrorResponse(res, 401, "unauthorized", error);
        return std::nullopt;
    }
    if (!required_role.empty() && user->role != required_role &&
        user->role != "admin")
    {
        ErrorResponse(res, 403, "forbidden", "insufficient role");
        return std::nullopt;
    }
    return user;
}

std::string ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return "";
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool RunMigrations(MySqlPool& pool, const std::string& directory)
{
    std::string error;
    std::unique_ptr<MySqlLease> lease = pool.Acquire(&error);
    if (lease == nullptr)
    {
        LogError("migrate_acquire", error);
        return false;
    }
    MYSQL* mysql = lease->get();

    if (!Execute(mysql,
                 "CREATE TABLE IF NOT EXISTS schema_migrations ("
                 "version VARCHAR(255) PRIMARY KEY, "
                 "applied_at DATETIME(6) NOT NULL)",
                 &error))
    {
        LogError("migrate_schema_table", error);
        return false;
    }

    std::set<std::string> applied;
    std::optional<SqlResult> existing =
        Query(mysql, "SELECT version FROM schema_migrations", &error);
    if (!existing.has_value())
    {
        LogError("migrate_read_versions", error);
        return false;
    }
    for (size_t i = 0; i < existing->size(); ++i)
    {
        applied.insert(existing->At(i, 0));
    }

    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (ec)
        {
            break;
        }
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (entry.path().extension() == ".sql")
        {
            files.push_back(entry.path().string());
        }
    }
    if (ec)
    {
        LogError("migrate_list", ec.message());
        return false;
    }
    std::sort(files.begin(), files.end());

    for (const std::string& file : files)
    {
        const std::string version = std::filesystem::path(file).filename().string();
        if (applied.count(version) != 0)
        {
            continue;
        }

        const std::string sql = ReadFile(file);
        if (sql.empty())
        {
            LogError("migrate_read", file);
            return false;
        }
        if (!Execute(mysql, sql, &error))
        {
            LogError("migrate_apply", version + ": " + error);
            return false;
        }
        if (!Execute(mysql,
                     "INSERT INTO schema_migrations (version, applied_at) "
                     "VALUES (" +
                         SqlString(mysql, version) + ", NOW(6))",
                     &error))
        {
            LogError("migrate_record", version + ": " + error);
            return false;
        }

        nlohmann::json fields;
        fields["version"] = version;
        LogEvent("info", "migration_applied", fields);
    }
    return true;
}

bool InsertSeedProduct(MYSQL* mysql,
                       const Product& product,
                       const std::string& error_prefix)
{
    std::string error;
    // Insert-only: re-running seed must not reset stock, price, status or
    // name of a product an operator has already edited.
    const std::string sql =
        "INSERT IGNORE INTO products (category_id, sku, name, description, "
        "price_cents, stock, status, created_at, updated_at) VALUES (" +
        std::to_string(product.category_id) + ", " +
        SqlString(mysql, product.sku) + ", " + SqlString(mysql, product.name) +
        ", " + SqlString(mysql, product.description) + ", " +
        std::to_string(product.price_cents) + ", " +
        std::to_string(product.stock) + ", 'active', NOW(6), NOW(6))";
    if (!Execute(mysql, sql, &error))
    {
        LogError(error_prefix, error);
        return false;
    }
    return true;
}

bool RunSeed(MySqlPool& pool, const Config& config)
{
    std::string error;
    std::unique_ptr<MySqlLease> lease = pool.Acquire(&error);
    if (lease == nullptr)
    {
        LogError("seed_acquire", error);
        return false;
    }
    MYSQL* mysql = lease->get();

    std::optional<SqlResult> admin = Query(
        mysql,
        "SELECT id FROM users WHERE email = " +
            SqlString(mysql, config.admin_email) + " LIMIT 1",
        &error);
    if (!admin.has_value())
    {
        LogError("seed_admin_lookup", error);
        return false;
    }
    if (admin->size() == 0)
    {
        const std::string password_hash = HashPassword(config.admin_password);
        if (password_hash.empty())
        {
            LogError("seed_admin_hash", "failed to hash admin password");
            return false;
        }
        if (!Execute(mysql,
                     "INSERT INTO users (email, password_hash, display_name, "
                     "role) VALUES (" +
                         SqlString(mysql, config.admin_email) + ", " +
                         SqlString(mysql, password_hash) +
                         ", 'CairnShop Admin', 'admin')",
                     &error))
        {
            LogError("seed_admin_insert", error);
            return false;
        }
    }

    struct SeedCategory
    {
        const char* name;
        const char* slug;
    };
    const SeedCategory categories[] = {
        {"Audio", "audio"},
        {"Home", "home"},
        {"Outdoor", "outdoor"},
    };
    for (const SeedCategory& category : categories)
    {
        if (!Execute(mysql,
                     "INSERT IGNORE INTO categories (name, slug) VALUES (" +
                         SqlString(mysql, category.name) + ", " +
                         SqlString(mysql, category.slug) + ")",
                     &error))
        {
            LogError("seed_category", error);
            return false;
        }
    }

    std::optional<SqlResult> category_rows =
        Query(mysql, "SELECT id, slug FROM categories", &error);
    if (!category_rows.has_value())
    {
        LogError("seed_category_lookup", error);
        return false;
    }
    std::vector<std::pair<uint64_t, std::string>> category_ids;
    for (size_t i = 0; i < category_rows->size(); ++i)
    {
        category_ids.emplace_back(std::stoull(category_rows->At(i, 0)),
                                  category_rows->At(i, 1));
    }
    auto category_id = [&category_ids](const std::string& slug) -> uint64_t {
        for (const auto& [id, value] : category_ids)
        {
            if (value == slug)
            {
                return id;
            }
        }
        return 0;
    };

    struct SeedProduct
    {
        const char* sku;
        const char* name;
        const char* description;
        const char* category;
        int64_t price_cents;
        int64_t stock;
    };
    const SeedProduct products[] = {
        {"AUD-001", "Studio Headphones", "Closed-back monitoring headphones",
         "audio", 12900, 40},
        {"AUD-002", "Portable Speaker", "Battery-powered speaker", "audio",
         7900, 60},
        {"HOME-001", "Desk Lamp", "Warm LED desk lamp", "home", 4500, 100},
        {"HOME-002", "Ceramic Mug", "350 ml stoneware mug", "home", 1900, 200},
        {"OUT-001", "Trail Bottle", "Insulated 750 ml bottle", "outdoor", 3900,
         80},
        {"OUT-002", "Camp Stool", "Folding aluminum stool", "outdoor", 5900,
         50},
    };
    for (const SeedProduct& seed : products)
    {
        Product product;
        product.category_id = category_id(seed.category);
        if (product.category_id == 0)
        {
            LogError("seed_product_category", seed.category);
            return false;
        }
        product.sku = seed.sku;
        product.name = seed.name;
        product.description = seed.description;
        product.price_cents = seed.price_cents;
        product.stock = seed.stock;
        product.status = "active";
        if (!InsertSeedProduct(mysql, product, "seed_product"))
        {
            return false;
        }
    }

    LogInfo("seed_completed");
    return true;
}

std::vector<CairnKvAddress> CairnKvSeeds(const Config& config)
{
    std::vector<CairnKvAddress> seeds;
    if (!config.cairnkv_addrs.empty())
    {
        size_t pos = 0;
        while (pos <= config.cairnkv_addrs.size())
        {
            const size_t end = config.cairnkv_addrs.find(',', pos);
            std::string part = config.cairnkv_addrs.substr(pos, end - pos);
            while (!part.empty() && part.front() == ' ')
            {
                part.erase(part.begin());
            }
            while (!part.empty() && part.back() == ' ')
            {
                part.pop_back();
            }
            const size_t colon = part.rfind(':');
            if (colon != std::string::npos && colon > 0)
            {
                try
                {
                    const int port = std::stoi(part.substr(colon + 1));
                    if (port > 0 && port <= 65535)
                    {
                        seeds.emplace_back(part.substr(0, colon),
                                           static_cast<uint16_t>(port));
                    }
                }
                catch (...)
                {
                }
            }
            if (end == std::string::npos)
            {
                break;
            }
            pos = end + 1;
        }
    }
    if (seeds.empty())
    {
        seeds.emplace_back(config.cairnkv_host,
                           static_cast<uint16_t>(config.cairnkv_port));
    }
    return seeds;
}

}

}

int main(int argc, char** argv)
{
    using namespace cairnshop;

    const std::string command = argc > 1 ? argv[1] : "serve";
    const Config config = LoadConfigFromEnv();

    if (command == "seed-config")
    {
        std::string error;
        const bool ok = SeedConfig(CairnKvSeeds(config), &error);
        if (!ok)
        {
            LogError("seed_config", error);
        }
        return ok ? 0 : 1;
    }

    if (mysql_library_init(0, nullptr, nullptr) != 0)
    {
        LogError("mysql_init", "mysql_library_init failed");
        return 1;
    }

    MySqlPool pool(config);
    std::string error;
    if (!pool.Initialize(&error))
    {
        LogError("mysql_pool", error);
        mysql_library_end();
        return 1;
    }

    if (command == "migrate")
    {
        const bool ok = RunMigrations(pool, config.migrations_dir);
        mysql_library_end();
        return ok ? 0 : 1;
    }

    AuthService auth(&pool, config.session_ttl_seconds);
    CatalogService catalog(&pool);

    if (command == "seed")
    {
        const bool ok = RunSeed(pool, config);
        mysql_library_end();
        return ok ? 0 : 1;
    }

    if (command != "serve")
    {
        LogError("usage",
                 "usage: cairnshop-backend [serve|migrate|seed|seed-config]");
        mysql_library_end();
        return 1;
    }

    ConfigService config_service(CairnKvSeeds(config),
                                 config.config_refresh_ms,
                                 config.config_max_stale_ms);
    std::string config_error;
    config_service.Start(&config_error);
    if (!config_error.empty())
    {
        LogError("config_refresh", config_error);
    }

    CartService cart_service(&pool);
    OrderService order_service(&pool, &config_service);
    AuditService audit(&pool);
    ConfigAdminService config_admin(&pool, &config_service, &audit);
    RateLimiter rate_limiter;
    Metrics metrics;

    httplib::Server server;

    server.set_pre_routing_handler(
        [&metrics](const httplib::Request& req, httplib::Response& res) {
            t_request_id = RequestId(req);
            t_request_start = std::chrono::steady_clock::now();
            metrics.OnRequest();

            const std::string method = req.method;
            if (method != "GET" && method != "HEAD" && method != "OPTIONS")
            {
                const std::string origin = req.get_header_value("Origin");
                if (!origin.empty())
                {
                    const size_t scheme = origin.find("://");
                    const std::string origin_host =
                        scheme == std::string::npos
                            ? origin
                            : origin.substr(scheme + 3);
                    if (origin_host != req.get_header_value("Host"))
                    {
                        metrics.OnStatus(403);
                        ErrorResponse(res, 403, "origin_rejected",
                                      "cross-origin write rejected");
                        return httplib::Server::HandlerResponse::Handled;
                    }
                }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    server.set_logger([&metrics](const httplib::Request& req,
                                 const httplib::Response& res) {
        const auto now = std::chrono::steady_clock::now();
        const auto latency_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - t_request_start)
                .count();
        metrics.OnStatus(res.status);

        nlohmann::json fields;
        fields["request_id"] = t_request_id;
        fields["method"] = req.method;
        fields["path"] = req.path;
        fields["status"] = res.status;
        fields["latency_ms"] = latency_ms;
        fields["remote_addr"] = req.remote_addr;
        LogEvent("info", "http_request", fields);
    });

    server.set_error_handler([](const httplib::Request& req,
                                httplib::Response& res) {
        if (!res.body.empty())
        {
            return;
        }
        const std::string code = res.status == 404 ? "not_found" : "error";
        ErrorResponse(res, res.status, code, "request failed");
        (void)req;
    });

    server.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ok", "text/plain");
    });

    server.Get("/readyz", [&pool, &config_service](const httplib::Request&,
                                                   httplib::Response& res) {
        if (pool.Ping() && config_service.Ready())
        {
            res.set_content("ok", "text/plain");
            return;
        }
        res.status = 503;
        res.set_content("not ready", "text/plain");
    });

    server.Get("/metrics", [&metrics](const httplib::Request&,
                                      httplib::Response& res) {
        res.set_content(metrics.RenderPrometheus(),
                        "text/plain; version=0.0.4");
    });

    server.Post("/api/v1/auth/register",
                [&auth, &config, &rate_limiter, &metrics](
                    const httplib::Request& req, httplib::Response& res) {
                    if (!CheckRate(rate_limiter,
                                   metrics,
                                   "register:" + ClientIp(req),
                                   60,
                                   res))
                    {
                        return;
                    }
                    std::optional<nlohmann::json> body = ParseBody(req, res);
                    if (!body.has_value())
                    {
                        return;
                    }
                    const std::string email =
                        GetString(*body, "email");
                    const std::string password =
                        GetString(*body, "password");
                    const std::string display_name =
                        GetString(*body, "display_name");

                    std::string error;
                    std::optional<Session> session = auth.Register(
                        email, password, display_name, &error);
                    if (!session.has_value())
                    {
                        ErrorResponse(res, 400, "register_failed", error);
                        return;
                    }
                    SetSessionCookie(res,
                                     session->token,
                                     config.session_ttl_seconds,
                                     config.cookie_secure);
                    JsonResponse(res, 201, UserJson(session->user));
                });

    server.Post("/api/v1/auth/login",
                [&auth, &config, &rate_limiter, &metrics](
                    const httplib::Request& req, httplib::Response& res) {
                    if (!CheckRate(rate_limiter,
                                   metrics,
                                   "login:" + ClientIp(req),
                                   5,
                                   res))
                    {
                        return;
                    }
                    std::optional<nlohmann::json> body = ParseBody(req, res);
                    if (!body.has_value())
                    {
                        return;
                    }
                    const std::string email =
                        GetString(*body, "email");
                    const std::string password =
                        GetString(*body, "password");

                    std::string error;
                    std::optional<Session> session =
                        auth.Login(email, password, &error);
                    if (!session.has_value())
                    {
                        ErrorResponse(res, 401, "login_failed", error);
                        return;
                    }
                    SetSessionCookie(res,
                                     session->token,
                                     config.session_ttl_seconds,
                                     config.cookie_secure);
                    JsonResponse(res, 200, UserJson(session->user));
                });

    server.Post("/api/v1/auth/logout",
                [&auth, &config](const httplib::Request& req,
                                 httplib::Response& res) {
                    const std::string token = GetCookie(req, kSessionCookie);
                    std::string error;
                    if (!auth.Logout(token, &error))
                    {
                        ErrorResponse(res, 500, "logout_failed", error);
                        return;
                    }
                    ClearSessionCookie(res, config.cookie_secure);
                    res.status = 204;
                });

    server.Get("/api/v1/auth/me",
               [&auth](const httplib::Request& req, httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth);
                   if (!user.has_value())
                   {
                       return;
                   }
                   JsonResponse(res, 200, UserJson(*user));
               });

    server.Get("/api/v1/catalog/categories",
               [&catalog](const httplib::Request&, httplib::Response& res) {
                   std::string error;
                   std::optional<std::vector<Category>> categories =
                       catalog.ListCategories(&error);
                   if (!categories.has_value())
                   {
                       ErrorResponse(res, 500, "catalog_error", error);
                       return;
                   }
                   nlohmann::json body;
                   body["categories"] = nlohmann::json::array();
                   for (const Category& category : *categories)
                   {
                       body["categories"].push_back(CategoryJson(category));
                   }
                   JsonResponse(res, 200, body);
               });

    server.Get("/api/v1/catalog/products",
               [&catalog](const httplib::Request& req,
                          httplib::Response& res) {
                   std::optional<uint64_t> category_id;
                   const std::string category_param =
                       req.get_param_value("category_id");
                   if (!category_param.empty())
                   {
                       try
                       {
                           category_id = std::stoull(category_param);
                       }
                       catch (...)
                       {
                           ErrorResponse(res, 400, "invalid_argument",
                                         "invalid category_id");
                           return;
                       }
                   }

                   int limit = 20;
                   int offset = 0;
                   const std::string limit_param =
                       req.get_param_value("limit");
                   const std::string offset_param =
                       req.get_param_value("offset");
                   try
                   {
                       if (!limit_param.empty())
                       {
                           limit = std::stoi(limit_param);
                       }
                       if (!offset_param.empty())
                       {
                           offset = std::stoi(offset_param);
                       }
                   }
                   catch (...)
                   {
                       ErrorResponse(res, 400, "invalid_argument",
                                     "invalid limit or offset");
                       return;
                   }

                   std::string error;
                   std::optional<std::vector<Product>> products =
                       catalog.ListProducts(category_id,
                                            req.get_param_value("q"),
                                            limit,
                                            offset,
                                            &error);
                   if (!products.has_value())
                   {
                       ErrorResponse(res, 500, "catalog_error", error);
                       return;
                   }
                   nlohmann::json body;
                   body["products"] = nlohmann::json::array();
                   for (const Product& product : *products)
                   {
                       body["products"].push_back(ProductJson(product));
                   }
                   body["limit"] = limit;
                   body["offset"] = offset;
                   JsonResponse(res, 200, body);
               });

    server.Get(R"(/api/v1/catalog/products/(\d+))",
               [&catalog](const httplib::Request& req,
                          httplib::Response& res) {
                   const uint64_t id =
                       std::stoull(req.matches[1].str());
                   std::string error;
                   std::optional<Product> product = catalog.GetProduct(id, &error);
                   if (!product.has_value())
                   {
                       ErrorResponse(res, 404, "not_found", error);
                       return;
                   }
                   if (product->status != "active")
                   {
                       ErrorResponse(res, 404, "not_found", "product not found");
                       return;
                   }
                   JsonResponse(res, 200, ProductJson(*product));
               });

    server.Get("/api/v1/cart",
               [&auth, &cart_service](const httplib::Request& req,
                                      httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth);
                   if (!user.has_value())
                   {
                       return;
                   }
                   std::string error;
                   std::optional<std::vector<CartItem>> items =
                       cart_service.Get(user->id, &error);
                   if (!items.has_value())
                   {
                       ErrorResponse(res, 500, "db_error", error);
                       return;
                   }
                   JsonResponse(res, 200, CartJson(*items));
               });

    server.Post("/api/v1/cart/items",
                [&auth, &cart_service](const httplib::Request& req,
                                       httplib::Response& res) {
                    std::optional<User> user = RequireUser(req, res, auth);
                    if (!user.has_value())
                    {
                        return;
                    }
                    std::optional<nlohmann::json> body = ParseBody(req, res);
                    if (!body.has_value())
                    {
                        return;
                    }
                    int64_t product_id = 0;
                    int64_t quantity = 0;
                    if (!GetInt64(*body, "product_id", &product_id) ||
                        !GetInt64(*body, "quantity", &quantity) ||
                        product_id <= 0 || quantity <= 0 || quantity > 99)
                    {
                        ErrorResponse(res, 400, "invalid_argument",
                                      "product_id and quantity are required");
                        return;
                    }

                    std::string error;
                    if (!cart_service.Add(user->id,
                                          static_cast<uint64_t>(product_id),
                                          static_cast<int>(quantity),
                                          &error))
                    {
                        ErrorResponse(res, 400, "cart_error", error);
                        return;
                    }
                    std::optional<std::vector<CartItem>> items =
                        cart_service.Get(user->id, &error);
                    if (!items.has_value())
                    {
                        ErrorResponse(res, 500, "db_error", error);
                        return;
                    }
                    JsonResponse(res, 200, CartJson(*items));
                });

    server.Patch(R"(/api/v1/cart/items/(\d+))",
                 [&auth, &cart_service](const httplib::Request& req,
                                        httplib::Response& res) {
                     std::optional<User> user = RequireUser(req, res, auth);
                     if (!user.has_value())
                     {
                         return;
                     }
                     std::optional<nlohmann::json> body = ParseBody(req, res);
                     if (!body.has_value())
                     {
                         return;
                     }
                     int64_t quantity = 0;
                     if (!GetInt64(*body, "quantity", &quantity) ||
                         quantity < 0 || quantity > 99)
                     {
                         ErrorResponse(res, 400, "invalid_argument",
                                       "quantity must be 0-99");
                         return;
                     }

                     const uint64_t product_id =
                         std::stoull(req.matches[1].str());
                     std::string error;
                     if (!cart_service.SetQuantity(user->id,
                                                   product_id,
                                                   static_cast<int>(quantity),
                                                   &error))
                     {
                         ErrorResponse(res, 400, "cart_error", error);
                         return;
                     }
                     std::optional<std::vector<CartItem>> items =
                         cart_service.Get(user->id, &error);
                     if (!items.has_value())
                     {
                         ErrorResponse(res, 500, "db_error", error);
                         return;
                     }
                     JsonResponse(res, 200, CartJson(*items));
                 });

    server.Delete(R"(/api/v1/cart/items/(\d+))",
                  [&auth, &cart_service](const httplib::Request& req,
                                         httplib::Response& res) {
                      std::optional<User> user = RequireUser(req, res, auth);
                      if (!user.has_value())
                      {
                          return;
                      }
                      const uint64_t product_id =
                          std::stoull(req.matches[1].str());
                      std::string error;
                      if (!cart_service.Remove(user->id, product_id, &error))
                      {
                          ErrorResponse(res, 500, "db_error", error);
                          return;
                      }
                      std::optional<std::vector<CartItem>> items =
                          cart_service.Get(user->id, &error);
                      if (!items.has_value())
                      {
                          ErrorResponse(res, 500, "db_error", error);
                          return;
                      }
                      JsonResponse(res, 200, CartJson(*items));
                  });

    server.Post("/api/v1/orders",
                [&auth, &order_service, &rate_limiter, &metrics](
                    const httplib::Request& req, httplib::Response& res) {
                    std::optional<User> user = RequireUser(req, res, auth);
                    if (!user.has_value())
                    {
                        return;
                    }
                    if (!CheckRate(rate_limiter,
                                   metrics,
                                   "checkout:" + std::to_string(user->id),
                                   10,
                                   res))
                    {
                        return;
                    }
                    const std::string idempotency_key =
                        req.get_header_value("Idempotency-Key");
                    const CheckoutResult result =
                        order_service.Checkout(user->id, idempotency_key);
                    metrics.OnCheckout(result.http_status == 201);
                    res.status = result.http_status;
                    res.set_content(result.body, "application/json");
                });

    server.Get("/api/v1/orders",
               [&auth, &order_service](const httplib::Request& req,
                                       httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth);
                   if (!user.has_value())
                   {
                       return;
                   }
                   int limit = 20;
                   int offset = 0;
                   try
                   {
                       const std::string limit_param =
                           req.get_param_value("limit");
                       const std::string offset_param =
                           req.get_param_value("offset");
                       if (!limit_param.empty())
                       {
                           limit = std::stoi(limit_param);
                       }
                       if (!offset_param.empty())
                       {
                           offset = std::stoi(offset_param);
                       }
                   }
                   catch (...)
                   {
                       ErrorResponse(res, 400, "invalid_argument",
                                     "invalid limit or offset");
                       return;
                   }

                   std::string error;
                   std::optional<std::vector<OrderView>> orders =
                       order_service.List(user->id, limit, offset, &error);
                   if (!orders.has_value())
                   {
                       ErrorResponse(res, 500, "db_error", error);
                       return;
                   }
                   nlohmann::json body;
                   body["orders"] = nlohmann::json::array();
                   for (const OrderView& order : *orders)
                   {
                       body["orders"].push_back(OrderToJson(order));
                   }
                   body["limit"] = limit;
                   body["offset"] = offset;
                   JsonResponse(res, 200, body);
               });

    server.Get(R"(/api/v1/orders/([A-Za-z0-9]+))",
               [&auth, &order_service](const httplib::Request& req,
                                       httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth);
                   if (!user.has_value())
                   {
                       return;
                   }
                   const bool is_ops =
                       user->role == "ops" || user->role == "admin";
                   std::string error;
                   std::optional<OrderView> order = order_service.Get(
                       user->id, req.matches[1].str(), is_ops, &error);
                   if (!order.has_value())
                   {
                       ErrorResponse(res, 404, "not_found", error);
                       return;
                   }
                   JsonResponse(res, 200, OrderToJson(*order));
               });

    server.Post(R"(/api/v1/orders/([A-Za-z0-9]+)/pay)",
                [&auth, &order_service](const httplib::Request& req,
                                        httplib::Response& res) {
                    std::optional<User> user = RequireUser(req, res, auth);
                    if (!user.has_value())
                    {
                        return;
                    }
                    const std::string order_no = req.matches[1].str();
                    int http_status = 500;
                    std::string error;
                    if (!order_service.Pay(
                            user->id, order_no, &error, &http_status))
                    {
                        ErrorResponse(res, http_status, "order_error", error);
                        return;
                    }
                    nlohmann::json body;
                    body["order_no"] = order_no;
                    body["status"] = "paid";
                    JsonResponse(res, 200, body);
                });

    server.Post(R"(/api/v1/orders/([A-Za-z0-9]+)/cancel)",
                [&auth, &order_service](const httplib::Request& req,
                                        httplib::Response& res) {
                    std::optional<User> user = RequireUser(req, res, auth);
                    if (!user.has_value())
                    {
                        return;
                    }
                    const std::string order_no = req.matches[1].str();
                    int http_status = 500;
                    std::string error;
                    if (!order_service.Cancel(
                            user->id, order_no, &error, &http_status))
                    {
                        ErrorResponse(res, http_status, "order_error", error);
                        return;
                    }
                    nlohmann::json body;
                    body["order_no"] = order_no;
                    body["status"] = "cancelled";
                    JsonResponse(res, 200, body);
                });

    server.Put("/api/v1/ops/config",
               [&auth, &config_admin, &rate_limiter, &metrics](
                   const httplib::Request& req, httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth, "ops");
                   if (!user.has_value())
                   {
                       return;
                   }
                   if (!CheckRate(rate_limiter,
                                  metrics,
                                  "ops:" + std::to_string(user->id),
                                  30,
                                  res))
                   {
                       return;
                   }
                   std::optional<nlohmann::json> body = ParseBody(req, res);
                   if (!body.has_value())
                   {
                       return;
                   }
                   const std::string key = GetString(*body, "key");
                   const auto value_it = body->find("value");
                   if (key.empty() || value_it == body->end() ||
                       !value_it->is_object())
                   {
                       ErrorResponse(res, 400, "invalid_argument",
                                     "key and an object value are required");
                       return;
                   }

                   std::string error;
                   int http_status = 500;
                   std::optional<nlohmann::json> document =
                       config_admin.Update(user->id,
                                           user->role,
                                           user->email,
                                           key,
                                           *value_it,
                                           RequestId(req),
                                           ClientIp(req),
                                           &error,
                                           &http_status);
                   if (!document.has_value())
                   {
                       ErrorResponse(res, http_status,
                                     "config_write_failed", error);
                       return;
                   }
                   JsonResponse(res, 200, *document);
               });

    server.Get("/api/v1/ops/config/history",
               [&auth, &config_admin](const httplib::Request& req,
                                      httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth, "ops");
                   if (!user.has_value())
                   {
                       return;
                   }
                   const std::string key = req.get_param_value("key");
                   if (key.empty())
                   {
                       ErrorResponse(res, 400, "invalid_argument",
                                     "key is required");
                       return;
                   }
                   std::string error;
                   int http_status = 500;
                   std::optional<nlohmann::json> document =
                       config_admin.History(key, &error, &http_status);
                   if (!document.has_value())
                   {
                       ErrorResponse(res, http_status,
                                     "config_history_failed", error);
                       return;
                   }
                   JsonResponse(res, 200, *document);
               });

    server.Post("/api/v1/ops/config/rollback",
                [&auth, &config_admin, &rate_limiter, &metrics](
                    const httplib::Request& req, httplib::Response& res) {
                    std::optional<User> user = RequireUser(req, res, auth, "ops");
                    if (!user.has_value())
                    {
                        return;
                    }
                    if (!CheckRate(rate_limiter,
                                   metrics,
                                   "ops:" + std::to_string(user->id),
                                   30,
                                   res))
                    {
                        return;
                    }
                    std::optional<nlohmann::json> body = ParseBody(req, res);
                    if (!body.has_value())
                    {
                        return;
                    }
                    const std::string key = GetString(*body, "key");
                    int64_t version = 0;
                    if (key.empty() || !GetInt64(*body, "version", &version) ||
                        version <= 0)
                    {
                        ErrorResponse(res, 400, "invalid_argument",
                                      "key and version are required");
                        return;
                    }

                    std::string error;
                    int http_status = 500;
                    std::optional<nlohmann::json> document =
                        config_admin.Rollback(user->id,
                                              user->role,
                                              user->email,
                                              key,
                                              static_cast<uint64_t>(version),
                                              RequestId(req),
                                              ClientIp(req),
                                              &error,
                                              &http_status);
                    if (!document.has_value())
                    {
                        ErrorResponse(res, http_status,
                                      "config_rollback_failed", error);
                        return;
                    }
                    JsonResponse(res, 200, *document);
                });

    server.Get("/api/v1/ops/audit",
               [&auth, &audit](const httplib::Request& req,
                               httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth, "ops");
                   if (!user.has_value())
                   {
                       return;
                   }
                   int limit = 50;
                   int offset = 0;
                   try
                   {
                       const std::string limit_param =
                           req.get_param_value("limit");
                       const std::string offset_param =
                           req.get_param_value("offset");
                       if (!limit_param.empty())
                       {
                           limit = std::stoi(limit_param);
                       }
                       if (!offset_param.empty())
                       {
                           offset = std::stoi(offset_param);
                       }
                   }
                   catch (...)
                   {
                       ErrorResponse(res, 400, "invalid_argument",
                                     "invalid limit or offset");
                       return;
                   }

                   std::string error;
                   std::optional<std::vector<AuditEntry>> entries =
                       audit.List(limit, offset, &error);
                   if (!entries.has_value())
                   {
                       ErrorResponse(res, 500, "db_error", error);
                       return;
                   }
                   nlohmann::json body;
                   body["entries"] = nlohmann::json::array();
                   for (const AuditEntry& entry : *entries)
                   {
                       nlohmann::json item;
                       item["id"] = entry.id;
                       item["actor_id"] = entry.actor_id;
                       item["actor_role"] = entry.actor_role;
                       item["action"] = entry.action;
                       item["target_type"] = entry.target_type;
                       item["target_id"] = entry.target_id;
                       item["request_id"] = entry.request_id;
                       item["ip"] = entry.ip;
                       item["created_at"] = entry.created_at;
                       nlohmann::json before = nlohmann::json::parse(
                           entry.before_json, nullptr, false);
                       item["before"] =
                           before.is_discarded()
                               ? nlohmann::json(nullptr)
                               : before;
                       nlohmann::json after = nlohmann::json::parse(
                           entry.after_json, nullptr, false);
                       item["after"] =
                           after.is_discarded()
                               ? nlohmann::json(nullptr)
                               : after;
                       body["entries"].push_back(std::move(item));
                   }
                   body["limit"] = limit;
                   body["offset"] = offset;
                   JsonResponse(res, 200, body);
               });

    server.Get("/api/v1/ops/status",
               [&auth, &pool, &config_service](const httplib::Request& req,
                                               httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth, "ops");
                   if (!user.has_value())
                   {
                       return;
                   }

                   nlohmann::json body;
                   body["mysql_ok"] = pool.Ping();

                   const ConfigSnapshot snapshot = config_service.Snapshot();
                   body["config"]["age_ms"] = snapshot.age_ms;
                   body["config"]["stale"] = snapshot.stale;
                   body["config"]["connected"] = snapshot.connected;
                   body["config"]["keys"] = snapshot.values.size();

                   std::string error;
                   std::optional<cairnkv::ClusterStatus> cluster =
                       config_service.GetClusterStatus(&error);
                   if (cluster.has_value())
                   {
                       const char* role = "follower";
                       if (cluster->role == cairnkv::NodeRole::kLeader)
                       {
                           role = "leader";
                       }
                       else if (cluster->role == cairnkv::NodeRole::kCandidate)
                       {
                           role = "candidate";
                       }
                       body["cairnkv"]["ok"] = true;
                       body["cairnkv"]["role"] = role;
                       body["cairnkv"]["term"] = cluster->term;
                       body["cairnkv"]["leader_id"] = cluster->leader_id;
                       body["cairnkv"]["commit_index"] = cluster->commit_index;
                       body["cairnkv"]["last_applied"] =
                           cluster->last_applied;
                       body["cairnkv"]["log_last_index"] =
                           cluster->log_last_index;
                       body["cairnkv"]["wal_bytes"] = cluster->wal_bytes;
                   }
                   else
                   {
                       body["cairnkv"]["ok"] = false;
                       body["cairnkv"]["error"] = error;
                   }

                   JsonResponse(res, 200, body);
               });

    server.Get("/api/v1/ops/catalog/products",
               [&auth, &catalog](const httplib::Request& req,
                                 httplib::Response& res) {
                   std::optional<User> user = RequireUser(req, res, auth, "ops");
                   if (!user.has_value())
                   {
                       return;
                   }

                   std::optional<uint64_t> category_id;
                   const std::string category_param =
                       req.get_param_value("category_id");
                   if (!category_param.empty())
                   {
                       try
                       {
                           category_id = std::stoull(category_param);
                       }
                       catch (...)
                       {
                           ErrorResponse(res, 400, "invalid_argument",
                                         "invalid category_id");
                           return;
                       }
                   }

                   int limit = 50;
                   int offset = 0;
                   try
                   {
                       const std::string limit_param =
                           req.get_param_value("limit");
                       const std::string offset_param =
                           req.get_param_value("offset");
                       if (!limit_param.empty())
                       {
                           limit = std::stoi(limit_param);
                       }
                       if (!offset_param.empty())
                       {
                           offset = std::stoi(offset_param);
                       }
                   }
                   catch (...)
                   {
                       ErrorResponse(res, 400, "invalid_argument",
                                     "invalid limit or offset");
                       return;
                   }

                   std::string error;
                   std::optional<std::vector<Product>> products =
                       catalog.ListProducts(category_id,
                                            req.get_param_value("q"),
                                            limit,
                                            offset,
                                            &error,
                                            false);
                   if (!products.has_value())
                   {
                       ErrorResponse(res, 500, "db_error", error);
                       return;
                   }
                   nlohmann::json body;
                   body["products"] = nlohmann::json::array();
                   for (const Product& product : *products)
                   {
                       body["products"].push_back(ProductJson(product));
                   }
                   body["limit"] = limit;
                   body["offset"] = offset;
                   JsonResponse(res, 200, body);
               });

    server.Get("/api/v1/ops/config",
               [&auth, &config_service](const httplib::Request& req,
                                        httplib::Response& res) {
                   if (!RequireUser(req, res, auth, "ops").has_value())
                   {
                       return;
                   }

                   const std::string key = req.get_param_value("key");
                   if (!key.empty())
                   {
                       std::string error;
                       const auto value = config_service.GetLatest(key, &error);
                       if (!value.has_value())
                       {
                           ErrorResponse(res, 503, "config_unavailable", error);
                           return;
                       }
                       nlohmann::json body;
                       body["key"] = key;
                       body["value"] = value->first;
                       body["applied_index"] = value->second;
                       JsonResponse(res, 200, body);
                       return;
                   }

                   const ConfigSnapshot snapshot = config_service.Snapshot();
                   nlohmann::json body;
                   body["age_ms"] = snapshot.age_ms;
                   body["stale"] = snapshot.stale;
                   body["connected"] = snapshot.connected;
                   body["keys"] = nlohmann::json::object();
                   for (const auto& [name, value] : snapshot.values)
                   {
                       body["keys"][name] = value;
                   }
                   JsonResponse(res, 200, body);
               });

    server.Post("/api/v1/ops/catalog/categories",
                [&auth, &catalog, &audit, &rate_limiter, &metrics](
                    const httplib::Request& req, httplib::Response& res) {
                    std::optional<User> user =
                        RequireUser(req, res, auth, "ops");
                    if (!user.has_value())
                    {
                        return;
                    }
                    if (!CheckRate(rate_limiter,
                                   metrics,
                                   "ops:" + std::to_string(user->id),
                                   30,
                                   res))
                    {
                        return;
                    }
                    std::optional<nlohmann::json> body = ParseBody(req, res);
                    if (!body.has_value())
                    {
                        return;
                    }
                    const std::string name =
                        GetString(*body, "name");
                    const std::string slug =
                        GetString(*body, "slug");
                    if (name.empty() || name.size() > 128 ||
                        !IsValidSlug(slug))
                    {
                        ErrorResponse(res, 400, "invalid_argument",
                                      "invalid name or slug");
                        return;
                    }
                    uint64_t id = 0;
                    std::string error;
                    if (!catalog.CreateCategory(name, slug, &id, &error))
                    {
                        ErrorResponse(res, 409, "category_create_failed", error);
                        return;
                    }
                    nlohmann::json response;
                    response["id"] = id;
                    response["name"] = name;
                    response["slug"] = slug;

                    std::string audit_error;
                    if (!audit.Record(user->id,
                                      user->role,
                                      "catalog.category.create",
                                      "category",
                                      std::to_string(id),
                                      "",
                                      response.dump(),
                                      RequestId(req),
                                      ClientIp(req),
                                      &audit_error))
                    {
                        LogError("audit_write", audit_error);
                    }
                    JsonResponse(res, 201, response);
                });

    server.Post("/api/v1/ops/catalog/products",
                [&auth, &catalog, &audit, &rate_limiter, &metrics](
                    const httplib::Request& req, httplib::Response& res) {
                    std::optional<User> user =
                        RequireUser(req, res, auth, "ops");
                    if (!user.has_value())
                    {
                        return;
                    }
                    if (!CheckRate(rate_limiter,
                                   metrics,
                                   "ops:" + std::to_string(user->id),
                                   30,
                                   res))
                    {
                        return;
                    }
                    std::optional<nlohmann::json> body = ParseBody(req, res);
                    if (!body.has_value())
                    {
                        return;
                    }

                    Product product;
                    product.sku = GetString(*body, "sku");
                    product.name = GetString(*body, "name");
                    product.description =
                        GetString(*body, "description");
                    product.status =
                        GetString(*body, "status", "draft");

                    int64_t category_id = 0;
                    int64_t price_cents = -1;
                    int64_t stock = -1;
                    if (!GetInt64(*body, "category_id", &category_id) ||
                        !GetInt64(*body, "price_cents", &price_cents) ||
                        !GetInt64(*body, "stock", &stock))
                    {
                        ErrorResponse(res, 400, "invalid_argument",
                                      "category_id, price_cents and stock "
                                      "must be integers");
                        return;
                    }
                    if (product.sku.empty() || product.name.empty() ||
                        category_id <= 0 || price_cents < 0 || stock < 0 ||
                        !IsValidProductStatus(product.status))
                    {
                        ErrorResponse(res, 400, "invalid_argument",
                                      "invalid product fields");
                        return;
                    }
                    product.category_id = static_cast<uint64_t>(category_id);
                    product.price_cents = price_cents;
                    product.stock = stock;

                    std::string error;
                    uint64_t id = 0;
                    if (!catalog.CreateProduct(product, &id, &error))
                    {
                        ErrorResponse(res, 409, "product_create_failed", error);
                        return;
                    }
                    product.id = id;
                    const nlohmann::json after = ProductJson(product);

                    std::string audit_error;
                    if (!audit.Record(user->id,
                                      user->role,
                                      "catalog.product.create",
                                      "product",
                                      std::to_string(id),
                                      "",
                                      after.dump(),
                                      RequestId(req),
                                      ClientIp(req),
                                      &audit_error))
                    {
                        LogError("audit_write", audit_error);
                    }
                    JsonResponse(res, 201, after);
                });

    server.Patch(R"(/api/v1/ops/catalog/products/(\d+))",
                 [&auth, &catalog, &audit, &rate_limiter, &metrics](
                     const httplib::Request& req, httplib::Response& res) {
                     std::optional<User> user =
                         RequireUser(req, res, auth, "ops");
                     if (!user.has_value())
                     {
                         return;
                     }
                     if (!CheckRate(rate_limiter,
                                    metrics,
                                    "ops:" + std::to_string(user->id),
                                    30,
                                    res))
                     {
                         return;
                     }
                     const uint64_t id = std::stoull(req.matches[1].str());

                     std::string error;
                     std::optional<Product> existing =
                         catalog.GetProduct(id, &error);
                     if (!existing.has_value())
                     {
                         ErrorResponse(res, 404, "not_found", error);
                         return;
                     }
                     const nlohmann::json before = ProductJson(*existing);

                     std::optional<nlohmann::json> body = ParseBody(req, res);
                     if (!body.has_value())
                     {
                         return;
                     }

                     Product product = *existing;
                     if (body->contains("sku"))
                     {
                         product.sku = GetString(*body, "sku");
                     }
                     if (body->contains("name"))
                     {
                         product.name = GetString(*body, "name");
                     }
                     if (body->contains("description"))
                     {
                         product.description =
                             GetString(*body, "description");
                     }
                     if (body->contains("status"))
                     {
                         product.status =
                             GetString(*body, "status");
                     }
                     int64_t value = 0;
                     if (body->contains("category_id"))
                     {
                         if (!GetInt64(*body, "category_id", &value) ||
                             value <= 0)
                         {
                             ErrorResponse(res, 400, "invalid_argument",
                                           "invalid category_id");
                             return;
                         }
                         product.category_id = static_cast<uint64_t>(value);
                     }
                     if (body->contains("price_cents"))
                     {
                         if (!GetInt64(*body, "price_cents", &value) ||
                             value < 0)
                         {
                             ErrorResponse(res, 400, "invalid_argument",
                                           "invalid price_cents");
                             return;
                         }
                         product.price_cents = value;
                     }
                     if (body->contains("stock"))
                     {
                         if (!GetInt64(*body, "stock", &value) || value < 0)
                         {
                             ErrorResponse(res, 400, "invalid_argument",
                                           "invalid stock");
                             return;
                         }
                         product.stock = value;
                     }
                     if (product.sku.empty() || product.name.empty() ||
                         !IsValidProductStatus(product.status))
                     {
                         ErrorResponse(res, 400, "invalid_argument",
                                       "invalid product fields");
                         return;
                     }

                     if (!catalog.UpdateProduct(product, &error))
                     {
                         ErrorResponse(res, 409, "product_update_failed", error);
                         return;
                     }

                     const nlohmann::json after = ProductJson(product);
                     std::string audit_error;
                     if (!audit.Record(user->id,
                                       user->role,
                                       "catalog.product.update",
                                       "product",
                                       std::to_string(id),
                                       before.dump(),
                                       after.dump(),
                                       RequestId(req),
                                       ClientIp(req),
                                       &audit_error))
                     {
                         LogError("audit_write", audit_error);
                     }
                     JsonResponse(res, 200, after);
                 });

    nlohmann::json started;
    started["port"] = config.http_port;
    started["mysql_host"] = config.mysql_host;
    started["mysql_database"] = config.mysql_database;
    LogEvent("info", "server_started", started);

    const bool listening = server.listen(config.http_host.c_str(),
                                         config.http_port);
    config_service.Stop();
    mysql_library_end();
    return listening ? 0 : 1;
}
