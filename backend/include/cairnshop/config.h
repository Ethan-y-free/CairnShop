#pragma once

#include <string>

namespace cairnshop
{

struct Config
{
    std::string mysql_host = "127.0.0.1";
    int mysql_port = 3306;
    std::string mysql_user = "cairnshop";
    std::string mysql_password;
    std::string mysql_database = "cairnshop";
    int mysql_pool_size = 4;

    std::string http_host = "0.0.0.0";
    int http_port = 8080;

    int session_ttl_seconds = 7 * 24 * 3600;
    bool cookie_secure = false;

    std::string admin_email = "admin@cairnshop.local";
    std::string admin_password = "admin123456";

    std::string migrations_dir = "db/migrations";
};

Config LoadConfigFromEnv();

}
