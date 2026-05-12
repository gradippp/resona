#pragma once
#include <string>
#include <vector>

namespace utils {

static inline std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string res;
    res.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (data[i] << 16) + (i + 1 < len ? data[i + 1] << 8 : 0) + (i + 2 < len ? data[i + 2] : 0);
        res.push_back(chars[(val >> 18) & 0x3F]);
        res.push_back(chars[(val >> 12) & 0x3F]);
        res.push_back(i + 1 < len ? chars[(val >> 6) & 0x3F] : '=');
        res.push_back(i + 2 < len ? chars[val & 0x3F] : '=');
    }
    return res;
}

static inline std::string base64_encode(const std::vector<uint8_t>& data) {
    return base64_encode(data.data(), data.size());
}

} // namespace utils
