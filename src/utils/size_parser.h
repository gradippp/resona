#pragma once
#include <string>

namespace utils {

/**
 * Parses a size string (e.g., "1G", "500M", "10K") into absolute byte values.
 * Returns 0 if parsing fails or if the string is empty.
 * Supported suffixes: K/KB, M/MB, G/GB.
 */
long long parse_size_string(const std::string& size_str);

} // namespace utils
