#include "media_service.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include "crow/logging.h"

// Define implementations for dr_libs (only once in one cpp file)
#define DR_WAV_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "../../third_party/dr_libs/dr_flac.h"

namespace services {

MediaResult MediaService::extract_waveform(const std::string& filepath, int resolution) {
    MediaResult res;
    
    if (!std::filesystem::exists(filepath)) {
        res.error_message = "File not found";
        return res;
    }

    res.file_size = std::filesystem::file_size(filepath);
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    float* pSampleData = nullptr;
    drwav_uint64 totalSampleCount = 0;
    unsigned int channels = 0;
    unsigned int sampleRate = 0;

    if (ext == ".wav") {
        res.format = "WAV";
        pSampleData = drwav_open_file_and_read_pcm_frames_f32(filepath.c_str(), &channels, &sampleRate, &totalSampleCount, NULL);
    } else if (ext == ".mp3") {
        res.format = "MP3";
        drmp3_config config;
        pSampleData = drmp3_open_file_and_read_pcm_frames_f32(filepath.c_str(), &config, &totalSampleCount, NULL);
        channels = config.channels;
        sampleRate = config.sampleRate;
    } else if (ext == ".flac") {
        res.format = "FLAC";
        pSampleData = drflac_open_file_and_read_pcm_frames_f32(filepath.c_str(), &channels, &sampleRate, &totalSampleCount, NULL);
    } else {
        res.error_message = "Unsupported file format: " + ext;
        return res;
    }

    if (!pSampleData) {
        res.error_message = "Failed to decode audio data";
        return res;
    }

    res.duration_seconds = (float)totalSampleCount / (float)sampleRate;
    res.waveform_data.resize(resolution, 0.0f);

    if (totalSampleCount > 0) {
        // Calculate peaks for each window using a Peak/RMS blend
        double samplesPerWindow = (double)totalSampleCount / (double)resolution;
        float globalMax = 0.0f;
        
        for (int i = 0; i < resolution; ++i) {
            size_t startFrame = (size_t)(i * samplesPerWindow);
            size_t endFrame = (size_t)((i + 1) * samplesPerWindow);
            if (endFrame > totalSampleCount) endFrame = (size_t)totalSampleCount;

            float maxAmp = 0.0f;
            float sumSq = 0.0f;
            int windowSampleCount = 0;

            for (size_t j = startFrame; j < endFrame; ++j) {
                for (unsigned int c = 0; c < channels; ++c) {
                    size_t index = j * channels + c;
                    if (index >= totalSampleCount * channels) break;
                    
                    float val = std::abs(pSampleData[index]);
                    if (val > maxAmp) maxAmp = val;
                    sumSq += val * val;
                    windowSampleCount++;
                }
            }
            
            float rms = (windowSampleCount > 0) ? std::sqrt(sumSq / windowSampleCount) : 0.0f;
            
            // Blend Peak (50%) and RMS (50%) for organic visual detail
            float blended = (maxAmp * 0.5f) + (rms * 0.5f);
            res.waveform_data[i] = blended;
            if (blended > globalMax) globalMax = blended;
        }

        // Global Normalization: Ensure the loudest part is 1.0
        if (globalMax > 0.0f) {
            for (float& p : res.waveform_data) {
                p /= globalMax;
            }
        }
    }

    // Free the decoded data
    if (ext == ".wav") drwav_free(pSampleData, NULL);
    else if (ext == ".mp3") drmp3_free(pSampleData, NULL);
    else if (ext == ".flac") drflac_free(pSampleData, NULL);

    res.success = true;
    CROW_LOG_INFO << "Extracted waveform for " << filepath << " (" << res.format << ", " << res.duration_seconds << "s, " << resolution << " peaks)";
    
    return res;
}

} // namespace services
