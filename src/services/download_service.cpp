#include "download_service.h"
#include <cpr/cpr.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include "crow/logging.h"

namespace services {

bool DownloadService::download_file(const std::string& url, const std::string& destination) {
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

    cpr::Response r = cpr::Get(
        cpr::Url{formatted_url},
        cpr::WriteCallback([&ofs](std::string_view data, intptr_t) {
            ofs.write(data.data(), data.size());
            return true;
        }),
        cpr::Redirect{true}, // Follow redirects (important for Dropbox)
        cpr::Timeout{5000}    // 5 second timeout
    );

    ofs.close();

    if (r.status_code >= 200 && r.status_code < 300) {
        CROW_LOG_INFO << "Successfully downloaded: " << destination << " (Status: " << r.status_code << ")";
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
    if (formatted.find("dropbox.com") != std::string::npos) {
        size_t pos = formatted.find("dl=0");
        if (pos != std::string::npos) {
            formatted.replace(pos, 4, "dl=1");
        } else if (formatted.find('?') == std::string::npos) {
            formatted += "?dl=1";
        } else if (formatted.find("dl=1") == std::string::npos) {
            formatted += "&dl=1";
        }
    }
    
    return formatted;
}

} // namespace services
