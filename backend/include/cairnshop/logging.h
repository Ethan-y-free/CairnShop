#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace cairnshop
{

// One JSON object per line; safe to call from any thread.
void LogEvent(const std::string& level,
              const std::string& event,
              const nlohmann::json& fields = nlohmann::json::object());

void LogInfo(const std::string& event);
void LogError(const std::string& event, const std::string& message);

}
