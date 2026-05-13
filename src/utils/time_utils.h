#pragma once
#include <string>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>

#ifdef _WIN32
#define timegm _mkgmtime
#endif

namespace utils {

/**
 * Parses a duration string (e.g., "5s", "10m", "2h", "500ms") into milliseconds.
 * Returns 0ms if parsing fails or string is empty.
 */
inline std::chrono::milliseconds parse_duration_ms(const std::string& duration_str) {
    if (duration_str.empty()) return std::chrono::milliseconds(0);
    try {
        size_t pos = 0;
        int value = std::stoi(duration_str, &pos);
        std::string unit = duration_str.substr(pos);
        
        // Normalize unit to lowercase for case-insensitive matching
        std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
        
        if (unit == "h") return std::chrono::hours(value);
        if (unit == "m") return std::chrono::minutes(value);
        if (unit == "s") return std::chrono::seconds(value);
        if (unit == "ms") return std::chrono::milliseconds(value);
    } catch (...) {}
    return std::chrono::milliseconds(0);
}

/**
 * Parses a duration string into seconds.
 */
inline std::chrono::seconds parse_duration_s(const std::string& duration_str) {
    return std::chrono::duration_cast<std::chrono::seconds>(parse_duration_ms(duration_str));
}

/**
 * Parses an HTTP date string (e.g., "Wed, 21 Oct 2015 07:28:00 GMT") and returns
 * the number of seconds from now until that date.
 * Returns default_seconds if parsing fails or date is in the past.
 */
inline int parse_http_date_to_seconds(const std::string& http_date, int default_seconds = 60) {
    if (http_date.empty()) return default_seconds;

    // Check if it's just a number (seconds)
    if (std::all_of(http_date.begin(), http_date.end(), [](unsigned char c) { return std::isdigit(c); })) {
        try {
            return std::stoi(http_date);
        } catch (...) {
            return default_seconds;
        }
    }

    std::tm tm = {};
    std::istringstream ss(http_date);
    ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    if (ss.fail()) return default_seconds;

    time_t target_time = timegm(&tm);
    time_t now = time(nullptr);
    return (std::max)(0, (int)difftime(target_time, now));
}

} // namespace utils
