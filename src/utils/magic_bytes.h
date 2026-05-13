#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include "crow/logging.h"

namespace utils {

/**
 * Detects the MIME type of a file using its magic bytes.
 * Returns an empty string if detection fails.
 */
inline std::string detect_content_type(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return "";

    // Read first 16 bytes for signature check
    std::vector<unsigned char> buffer(16, 0);
    file.read((char*)buffer.data(), buffer.size());
    size_t bytes_read = file.gcount();
    if (bytes_read < 4) return "";

    auto matches = [](const std::vector<unsigned char>& buf, const std::string& sig, size_t offset = 0) {
        if (buf.size() < offset + sig.size()) return false;
        return std::equal(sig.begin(), sig.end(), buf.begin() + offset);
    };

    // MP3 (ID3 or raw frames)
    if (matches(buffer, "ID3")) return "audio/mpeg";
    if (buffer[0] == 0xFF && (buffer[1] & 0xE0) == 0xE0) return "audio/mpeg";

    // WAV
    if (matches(buffer, "RIFF") && matches(buffer, "WAVE", 8)) return "audio/wav";

    // FLAC
    if (matches(buffer, "fLaC")) return "audio/flac";

    // MP4 / M4A (ftyp at offset 4)
    if (matches(buffer, "ftyp", 4)) return "audio/mp4";

    // JSON (starts with { or [)
    if (buffer[0] == '{' || buffer[0] == '[') return "application/json";

    return "";
}

} // namespace utils
