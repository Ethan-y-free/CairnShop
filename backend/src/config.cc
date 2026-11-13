#include "cairnshop/config.h"

#include <cstdlib>
#include <string>

namespace cairnshop
{

namespace
{

std::string EnvString(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    return value;
}

int EnvInt(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return fallback;
    }
}

bool EnvBool(const char* name, bool fallback)
{
    const std::string value = EnvString(name, "");
    if (value.empty())
    {
        return fallback;
    }
    return value == "1" || value == "true" || value == "TRUE" || value == "yes";
}

}

Config LoadConfigFromEnv()
{
    Config config;
    config.mysql_host =
        EnvString("CAIRNSHOP_MYSQL_HOST", config.mysql_host);
    config.mysql_port =
        EnvInt("CAIRNSHOP_MYSQL_PORT", config.mysql_port);
    config.mysql_user =
        EnvString("CAIRNSHOP_MYSQL_USER", config.mysql_user);
    config.mysql_password =
        EnvString("CAIRNSHOP_MYSQL_PASSWORD", config.mysql_password);
    config.mysql_database =
        EnvString("CAIRNSHOP_MYSQL_DATABASE", config.mysql_database);
    config.mysql_pool_size =
        EnvInt("CAIRNSHOP_MYSQL_POOL_SIZE", config.mysql_pool_size);

    config.http_host = EnvString("CAIRNSHOP_HTTP_HOST", config.http_host);
    config.http_port = EnvInt("CAIRNSHOP_HTTP_PORT", config.http_port);

    config.session_ttl_seconds =
        EnvInt("CAIRNSHOP_SESSION_TTL_SECONDS", config.session_ttl_seconds);
    config.cookie_secure =
        EnvBool("CAIRNSHOP_COOKIE_SECURE", config.cookie_secure);

    config.admin_email =
        EnvString("CAIRNSHOP_ADMIN_EMAIL", config.admin_email);
    config.admin_password =
        EnvString("CAIRNSHOP_ADMIN_PASSWORD", config.admin_password);
    config.migrations_dir =
        EnvString("CAIRNSHOP_MIGRATIONS_DIR", config.migrations_dir);

    if (config.mysql_pool_size < 1)
    {
        config.mysql_pool_size = 1;
    }
    if (config.session_ttl_seconds < 60)
    {
        config.session_ttl_seconds = 60;
    }
    return config;
}

}
