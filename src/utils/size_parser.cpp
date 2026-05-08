#include "size_parser.h"
#include <cctype>
#include <algorithm>
#include <string>

namespace utils {

long long parse_size_string(const std::string& size_str) {
    if (size_str.empty()) return 0;

    std::string s = size_str;
    // Remove whitespace and convert to uppercase
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);

    size_t last_digit_idx = s.find_last_of("0123456789");
    if (last_digit_idx == std::string::npos) return 0;

    long long value;
    try {
        value = std::stoll(s.substr(0, last_digit_idx + 1));
    } catch (...) {
        return 0;
    }

    std::string unit = s.substr(last_digit_idx + 1);
    
    if (unit == "K" || unit == "KB") return value * 1024ULL;
    if (unit == "M" || unit == "MB") return value * 1024ULL * 1024ULL;
    if (unit == "G" || unit == "GB") return value * 1024ULL * 1024ULL * 1024ULL;

    return value;
}

} // namespace utils
