#pragma once
#include <string>
#include <filesystem>
#include <algorithm>
#include <vector>

namespace utils {

/**
 * Validates that a path is within a base directory.
 * Prevents path traversal attacks.
 */
inline bool is_path_safe(const std::string& path, const std::string& base_dir) {
    try {
        std::filesystem::path p = std::filesystem::weakly_canonical(path);
        std::filesystem::path base = std::filesystem::weakly_canonical(base_dir);
        
        std::string p_str = p.string();
        std::string base_str = base.string();

        // Ensure base_str ends with a separator for correct prefix matching
        if (!base_str.empty() && base_str.back() != std::filesystem::path::preferred_separator) {
            base_str += std::filesystem::path::preferred_separator;
        }

        return p_str.compare(0, base_str.length(), base_str) == 0;
    } catch (...) {
        return false;
    }
}

struct HttpRange {
    size_t start = 0;
    size_t end = 0;
    bool has_end = false;
};

/**
 * Parses an HTTP Range header (e.g., "bytes=0-1023").
 * Returns true if parsing was successful.
 */
inline bool parse_range_header(const std::string& header, size_t file_size, HttpRange& range) {
    if (header.empty()) return false;

    std::string prefix = "bytes=";
    if (header.find(prefix) != 0) return false;

    std::string range_val = header.substr(prefix.length());
    size_t dash_pos = range_val.find('-');
    if (dash_pos == std::string::npos) return false;

    try {
        std::string start_str = range_val.substr(0, dash_pos);
        std::string end_str = range_val.substr(dash_pos + 1);

        if (start_str.empty()) {
            // Suffix range: bytes=-500
            if (end_str.empty()) return false;
            size_t suffix = std::stoull(end_str);
            range.start = (suffix > file_size) ? 0 : (file_size - suffix);
            range.end = file_size - 1;
            range.has_end = true;
        } else {
            range.start = std::stoull(start_str);
            if (!end_str.empty()) {
                range.end = std::stoull(end_str);
                range.has_end = true;
            } else {
                range.has_end = false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace utils
