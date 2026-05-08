#pragma once
#include <string>
#include <vector>

namespace services {

struct MediaResult {
    bool success = false;
    long long file_size = 0;
    std::string format = "";
    float duration_seconds = 0.0f;
    std::vector<float> waveform_data;
    std::string error_message = "";
};

class MediaService {
public:
    /**
     * Extracts metadata and waveform peaks from an audio file.
     * Supports WAV, MP3, and FLAC.
     */
    static MediaResult extract_waveform(const std::string& filepath, int resolution);
};

} // namespace services
