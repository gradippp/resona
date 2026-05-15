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

    /**
     * Transcodes an audio file to a target format using FFmpeg.
     * @param input_filepath Path to the source file.
     * @param output_filepath Path to save the transcoded file.
     * @param target_format Target extension/format (e.g., "mp3", "ogg").
     * @return true if successful, false otherwise.
     */
    static bool transcode_audio(const std::string& input_filepath, const std::string& output_filepath, const std::string& target_format);
};

} // namespace services
