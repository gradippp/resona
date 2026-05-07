#pragma once
#include "crow/logging.h"
#include <string>

namespace utils {

class CustomLogger : public crow::ILogHandler {
public:
    void log(const std::string& message, crow::LogLevel level) override;

private:
    std::string get_timestamp();
    std::string level_to_string(crow::LogLevel level);
};

} // namespace utils
