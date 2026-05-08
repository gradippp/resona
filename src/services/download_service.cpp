#include "download_service.h"
#include <cpr/cpr.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include "crow/logging.h"

namespace services {

bool DownloadService::download_file(const std::string& url, const std::string& destination, long long max_bytes) {
    std::string formatted_url = format_url(url);
    CROW_LOG_INFO << "Starting download from: " << formatted_url << " to " << destination;

    // Ensure directory exists
    std::filesystem::path dest_path(destination);
    if (dest_path.has_parent_path()) {
        std::filesystem::create_directories(dest_path.parent_path());
    }

    std::ofstream ofs(destination, std::ios::binary);
    if (!ofs.is_open()) {
        CROW_LOG_ERROR << "Failed to open destination file: " << destination;
        return false;
    }

    long long total_bytes = 0;
    bool size_exceeded = false;

    cpr::Response r = cpr::Get(
        cpr::Url{formatted_url},
        cpr::WriteCallback([&ofs, &total_bytes, max_bytes, &size_exceeded](std::string_view data, intptr_t) {
            if (max_bytes > 0 && (total_bytes + (long long)data.size() > max_bytes)) {
                size_exceeded = true;
                return false; // Abort download
            }
            ofs.write(data.data(), data.size());
            total_bytes += data.size();
            return true;
        }),
        cpr::Redirect{true} // Follow redirects (important for Dropbox)
        // Timeout removed to allow large media files
    );

    ofs.close();

    if (size_exceeded) {
        CROW_LOG_ERROR << "Download aborted: File exceeds maximum allowed size (" << max_bytes << " bytes).";
        std::filesystem::remove(destination);
        return false;
    }

    if (r.status_code >= 200 && r.status_code < 300) {
        CROW_LOG_INFO << "Successfully downloaded: " << destination << " (Status: " << r.status_code << ", Size: " << total_bytes << " bytes)";
        return true;
    } else {
        CROW_LOG_ERROR << "Failed to download: " << formatted_url << " (Status: " << r.status_code << ", Error: " << r.error.message << ")";
        std::filesystem::remove(destination); // Clean up partial file
        return false;
    }
}

std::string DownloadService::format_url(const std::string& url) {
    std::string formatted = url;
    
    // Handle Dropbox URLs: change dl=0 to dl=1
    // Matches dropbox.com URLs and handles query parameters/fragments robustly
    std::regex db_regex(R"((https?://(?:www\.)?dropbox\.com/[^?\s#]+)(\?[^#\s]*)?)", std::regex::icase);
    std::smatch match;

    if (std::regex_search(url, match, db_regex)) {
        std::string base_url = match[1].str();
        std::string query = match[2].str();
        
        if (query.empty()) {
            formatted = base_url + "?dl=1";
        } else if (query.find("dl=0") != std::string::npos) {
            formatted = base_url + std::regex_replace(query, std::regex("dl=0"), "dl=1");
        } else if (query.find("dl=1") == std::string::npos) {
            formatted = url + "&dl=1";
        }
    }
    
    return formatted;
}

} // namespace services
