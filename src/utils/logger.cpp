#include "logger.h"
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace utils {

void CustomLogger::log(const std::string& message, crow::LogLevel level) {
    if (message.find("Request: ") == 0 || message.find("Response: ") == 0) {
        return;
    }
    std::cerr << "[" << get_timestamp() << "] [" << std::this_thread::get_id() << "] [" 
              << std::left << std::setw(8) << level_to_string(level) << "] " << message << std::endl;
}

std::string CustomLogger::get_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string CustomLogger::level_to_string(crow::LogLevel level) {
    switch (level) {
        case crow::LogLevel::Debug:    return "DEBUG";
        case crow::LogLevel::Info:     return "INFO";
        case crow::LogLevel::Warning:  return "WARNING";
        case crow::LogLevel::Error:    return "ERROR";
        case crow::LogLevel::Critical: return "CRITICAL";
        default:                       return "UNKNOWN";
    }
}

} // namespace utils
