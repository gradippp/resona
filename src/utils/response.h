#pragma once
#include "crow.h"
#include <nlohmann/json.hpp>
#include <string>

namespace utils {

inline crow::response json_response(const nlohmann::json& j, int status = 200) {
    return crow::response(status, j.dump());
}

inline crow::response error_response(const std::string& message, int status = 400) {
    nlohmann::json j;
    j["error"] = message;
    return crow::response(status, j.dump());
}

} // namespace utils
