#include "cairnshop/logging.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace cairnshop
{

namespace
{

std::mutex g_log_mutex;

std::string UtcTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count() %
                        1000;

    std::tm tm{};
    gmtime_r(&seconds, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
        << std::setfill('0') << millis << 'Z';
    return out.str();
}

}

void LogEvent(const std::string& level,
              const std::string& event,
              const nlohmann::json& fields)
{
    nlohmann::json line;
    line["time"] = UtcTimestamp();
    line["level"] = level;
    line["event"] = event;
    if (fields.is_object())
    {
        for (const auto& [key, value] : fields.items())
        {
            line[key] = value;
        }
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::cout << line.dump() << '\n';
    std::cout.flush();
}

void LogInfo(const std::string& event)
{
    LogEvent("info", event);
}

void LogError(const std::string& event, const std::string& message)
{
    nlohmann::json fields;
    fields["message"] = message;
    LogEvent("error", event, fields);
}

}
