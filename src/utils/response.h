#pragma once
#include "crow.h"
#include <nlohmann/json.hpp>
#include <string>

namespace utils {

struct ProblemDetails {
    std::string type = "about:blank";
    std::string title;
    int status;
    std::string detail;
    std::string instance;

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["type"] = type;
        j["title"] = title;
        j["status"] = status;
        j["detail"] = detail;
        if (!instance.empty()) {
            j["instance"] = instance;
        }
        return j;
    }
};

inline std::string get_default_title(int status) {
    switch (status) {
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 415: return "Unsupported Media Type";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

inline crow::response json_response(const nlohmann::json& j, int status = 200) {
    crow::response res(status);
    res.set_header("Content-Type", "application/json");
    res.write(j.dump());
    return res;
}

inline crow::response error_response(const ProblemDetails& problem) {
    crow::response res(problem.status);
    res.set_header("Content-Type", "application/json");
    res.write(problem.to_json().dump());
    return res;
}

inline crow::response error_response(const std::string& detail, int status = 400, const std::string& title = "") {
    ProblemDetails problem;
    problem.status = status;
    problem.title = title.empty() ? get_default_title(status) : title;
    problem.detail = detail;
    return error_response(problem);
}

inline void send_error(crow::response& res, const std::string& detail, int status = 400, const std::string& title = "") {
    ProblemDetails problem;
    problem.status = status;
    problem.title = title.empty() ? get_default_title(status) : title;
    problem.detail = detail;
    res.code = status;
    res.set_header("Content-Type", "application/json");
    res.write(problem.to_json().dump());
    res.end();
}

} // namespace utils
