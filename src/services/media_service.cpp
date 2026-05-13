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
    CROW_LOG_INFO << "Extracting waveform for: " << filepath << " (Resolution: " << resolution << ")";
    
    if (!std::filesystem::exists(filepath)) {
        res.error_message = "File not found";
        CROW_LOG_ERROR << "Waveform extraction failed: File not found: " << filepath;
        return res;
    }

    res.file_size = std::filesystem::file_size(filepath);
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 totalFrameCount = 0;

    drwav wav;
    drmp3 mp3;
    drflac* pFlac = nullptr;
    bool opened = false;

    if (ext == ".wav") {
        if (drwav_init_file(&wav, filepath.c_str(), NULL)) {
            channels = wav.channels;
            sampleRate = wav.sampleRate;
            totalFrameCount = wav.totalPCMFrameCount;
            res.format = "WAV";
            opened = true;
        }
    } else if (ext == ".mp3") {
        if (drmp3_init_file(&mp3, filepath.c_str(), NULL)) {
            channels = mp3.channels;
            sampleRate = mp3.sampleRate;
            totalFrameCount = drmp3_get_pcm_frame_count(&mp3);
            res.format = "MP3";
            opened = true;
        }
    } else if (ext == ".flac") {
        pFlac = drflac_open_file(filepath.c_str(), NULL);
        if (pFlac) {
            channels = pFlac->channels;
            sampleRate = pFlac->sampleRate;
            totalFrameCount = pFlac->totalPCMFrameCount;
            res.format = "FLAC";
            opened = true;
        }
    } else {
        res.error_message = "Unsupported file format: " + ext;
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message;
        return res;
    }

    if (!opened) {
        res.error_message = "Failed to open audio file";
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message << ": " << filepath;
        return res;
    }

    if (totalFrameCount == 0) {
        // For some files (like some MP3s), frame count might not be in the header.
        // In a real production system, we might need a pre-scan, but for now we fail if unknown.
        res.error_message = "Unknown or zero frame count";
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message << ": " << filepath;
        if (ext == ".wav") drwav_uninit(&wav);
        else if (ext == ".mp3") drmp3_uninit(&mp3);
        else if (ext == ".flac") drflac_close(pFlac);
        return res;
    }

    res.duration_seconds = (float)totalFrameCount / (float)sampleRate;
    
    const size_t CHUNK_SIZE = 8192;
    std::vector<float> chunkBuffer(CHUNK_SIZE * channels);
    
    struct FloatPeak { float min; float max; };
    std::vector<FloatPeak> tempPeaks(resolution, {0.0f, 0.0f});
    
    double samplesPerWindow = (double)totalFrameCount / (double)resolution;
    float currentMin = 0.0f;
    float currentMax = 0.0f;
    double samplesInCurrentWindow = 0.0;
    int currentWindowIndex = 0;
    float globalMax = 0.0f;
    const float gamma = 0.5f;

    auto apply_gamma = [&](float x) {
        if (x == 0.0f) return 0.0f;
        return (x > 0.0f ? 1.0f : -1.0f) * std::pow(std::abs(x), gamma);
    };

    size_t framesRead = 0;
    bool processing = true;
    while (processing) {
        if (ext == ".wav") framesRead = drwav_read_pcm_frames_f32(&wav, CHUNK_SIZE, chunkBuffer.data());
        else if (ext == ".mp3") framesRead = drmp3_read_pcm_frames_f32(&mp3, CHUNK_SIZE, chunkBuffer.data());
        else if (ext == ".flac") framesRead = drflac_read_pcm_frames_f32(pFlac, CHUNK_SIZE, chunkBuffer.data());
        
        if (framesRead == 0) break;

        for (size_t f = 0; f < framesRead; ++f) {
            float frameMin = 1.0f;
            float frameMax = -1.0f;
            for (unsigned int c = 0; c < channels; ++c) {
                float val = chunkBuffer[f * channels + c];
                if (val < frameMin) frameMin = val;
                if (val > frameMax) frameMax = val;
            }

            if (frameMin < currentMin) currentMin = frameMin;
            if (frameMax > currentMax) currentMax = frameMax;
            
            samplesInCurrentWindow += 1.0;

            if (samplesInCurrentWindow >= samplesPerWindow && currentWindowIndex < resolution) {
                float sMin = apply_gamma(currentMin);
                float sMax = apply_gamma(currentMax);

                tempPeaks[currentWindowIndex] = {sMin, sMax};
                
                float absMax = std::max(std::abs(sMin), std::abs(sMax));
                if (absMax > globalMax) globalMax = absMax;

                currentMin = 0.0f;
                currentMax = 0.0f;
                samplesInCurrentWindow -= samplesPerWindow;
                currentWindowIndex++;
            }
        }
    }

    // Cleanup decoders
    if (ext == ".wav") drwav_uninit(&wav);
    else if (ext == ".mp3") drmp3_uninit(&mp3);
    else if (ext == ".flac") drflac_close(pFlac);

    // Final normalization, legacy population, and quantization
    res.waveform_data.resize(resolution, 0.0f);
    res.waveform_peaks.resize(resolution);
    
    if (globalMax > 0.0f) {
        for (int i = 0; i < resolution; ++i) {
            float nMin = tempPeaks[i].min / globalMax;
            float nMax = tempPeaks[i].max / globalMax;
            
            res.waveform_peaks[i].minPeak = (int16_t)(nMin * 32767.0f);
            res.waveform_peaks[i].maxPeak = (int16_t)(nMax * 32767.0f);
            
            // Legacy: store scaled positive max peak [0, 1]
            res.waveform_data[i] = std::max(0.0f, nMax);
        }
    }

    res.success = true;
    CROW_LOG_INFO << "Extracted streaming waveform for " << filepath << " (" << res.format << ", " << res.duration_seconds << "s, " << resolution << " peaks)";
    
    return res;
}

} // namespace services
