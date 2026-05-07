#pragma once
#include <string>

namespace services {

class DownloadService {
public:
    /**
     * Downloads a file from a URL to a local destination.
     * Automatically handles Dropbox URL formatting (dl=0 to dl=1).
     */
    static bool download_file(const std::string& url, const std::string& destination);

private:
    static std::string format_url(const std::string& url);
};

} // namespace services
