#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace services {

struct WaveformPointInt16 {
    int16_t minPeak;
    int16_t maxPeak;
};

struct MediaResult {
    bool success = false;
    long long file_size = 0;
    std::string format = "";
    float duration_seconds = 0.0f;
    std::vector<float> waveform_data;
    std::vector<WaveformPointInt16> waveform_peaks;
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
