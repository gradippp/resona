#include "download_service.h"
#include <cpr/cpr.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include "crow/logging.h"

namespace services {

static int parse_retry_after(const std::string& retry_after) {
    if (retry_after.empty()) return 60;

    if (std::all_of(retry_after.begin(), retry_after.end(), [](unsigned char c) { return std::isdigit(c); })) {
        try {
            return std::stoi(retry_after);
        } catch (...) {
            return 60;
        }
    }

    std::tm tm = {};
    std::istringstream ss(retry_after);
    ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S GMT");
    if (ss.fail()) return 60;

    time_t target_time = _mkgmtime(&tm);
    time_t now = time(nullptr);
    return (std::max)(0, (int)difftime(target_time, now));
}

DownloadResult DownloadService::download_file(const std::string& url, const std::string& destination, long long max_bytes) {
    DownloadResult res;
    std::string formatted_url = format_url(url);
    CROW_LOG_INFO << "Starting download from: " << formatted_url << " to " << destination;

    // Ensure directory exists
    std::filesystem::path dest_path(destination);
    if (dest_path.has_parent_path()) {
        std::filesystem::create_directories(dest_path.parent_path());
    }

    std::ofstream ofs(destination, std::ios::binary);
    if (!ofs.is_open()) {
        res.error_message = "Failed to open destination file";
        CROW_LOG_ERROR << res.error_message << ": " << destination;
        return res;
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
        cpr::Redirect{true}
    );

    ofs.close();

    if (size_exceeded) {
        res.error_message = "File exceeds maximum allowed size";
        CROW_LOG_ERROR << "Download aborted: " << res.error_message << " (" << max_bytes << " bytes).";
        std::filesystem::remove(destination);
        return res;
    }

    if (r.status_code == 429 || r.status_code == 503) {
        res.rate_limited = true;
        res.retry_after_seconds = parse_retry_after(r.header["Retry-After"]);
        res.error_message = "Rate limited or service unavailable (HTTP " + std::to_string(r.status_code) + ")";
        CROW_LOG_WARNING << res.error_message << ". Retrying after " << res.retry_after_seconds << "s";
        std::filesystem::remove(destination);
        return res;
    }

    if (r.status_code >= 200 && r.status_code < 300) {
        CROW_LOG_INFO << "Successfully downloaded: " << destination << " (Status: " << r.status_code << ", Size: " << total_bytes << " bytes)";
        res.success = true;
        return res;
    } else {
        res.error_message = r.error.message.empty() ? "HTTP Status " + std::to_string(r.status_code) : r.error.message;
        CROW_LOG_ERROR << "Failed to download: " << formatted_url << " (" << res.error_message << ")";
        std::filesystem::remove(destination);
        return res;
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
