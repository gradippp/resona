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

// RAII helper for dr_libs decoders
struct AudioDecoder {
    enum Type { NONE, WAV, MP3, FLAC };
    Type type = NONE;
    drwav wav;
    drmp3 mp3;
    drflac* flac = nullptr;

    ~AudioDecoder() {
        if (type == WAV) drwav_uninit(&wav);
        else if (type == MP3) drmp3_uninit(&mp3);
        else if (type == FLAC && flac) drflac_close(flac);
    }

    size_t read_f32(float* buffer, size_t frames) {
        if (type == WAV) return drwav_read_pcm_frames_f32(&wav, frames, buffer);
        if (type == MP3) return drmp3_read_pcm_frames_f32(&mp3, frames, buffer);
        if (type == FLAC) return drflac_read_pcm_frames_f32(flac, frames, buffer);
        return 0;
    }
};

MediaResult MediaService::extract_waveform(const std::string& filepath, int resolution) {
    MediaResult res;
    if (resolution <= 0) {
        res.error_message = "Invalid resolution";
        return res;
    }

    CROW_LOG_INFO << "Extracting waveform for: " << filepath << " (Resolution: " << resolution << ")";
    
    std::error_code ec;
    if (!std::filesystem::exists(filepath, ec)) {
        res.error_message = "File not found";
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message << ": " << filepath;
        return res;
    }

    res.file_size = (long long)std::filesystem::file_size(filepath, ec);
    std::string ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 totalFrameCount = 0;
    AudioDecoder decoder;

    // 1. Open Decoder
    bool opened = false;
    if (ext == ".wav") {
        if (drwav_init_file(&decoder.wav, filepath.c_str(), NULL)) {
            decoder.type = AudioDecoder::WAV;
            channels = decoder.wav.channels;
            sampleRate = decoder.wav.sampleRate;
            totalFrameCount = decoder.wav.totalPCMFrameCount;
            res.format = "WAV";
            opened = true;
        }
    } else if (ext == ".mp3") {
        if (drmp3_init_file(&decoder.mp3, filepath.c_str(), NULL)) {
            decoder.type = AudioDecoder::MP3;
            channels = decoder.mp3.channels;
            sampleRate = decoder.mp3.sampleRate;
            totalFrameCount = drmp3_get_pcm_frame_count(&decoder.mp3);
            res.format = "MP3";
            opened = true;
        }
    } else if (ext == ".flac") {
        decoder.flac = drflac_open_file(filepath.c_str(), NULL);
        if (decoder.flac) {
            decoder.type = AudioDecoder::FLAC;
            channels = decoder.flac->channels;
            sampleRate = decoder.flac->sampleRate;
            totalFrameCount = decoder.flac->totalPCMFrameCount;
            res.format = "FLAC";
            opened = true;
        }
    }

    if (!opened) {
        res.error_message = "Unsupported file format: " + ext;
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message << " (" << filepath << ")";
        return res;
    }

    if (totalFrameCount == 0 || sampleRate == 0) {
        res.error_message = "Empty audio file or invalid sample rate";
        CROW_LOG_ERROR << "Waveform extraction failed: " << res.error_message;
        return res;
    }

    res.duration_seconds = (float)totalFrameCount / (float)sampleRate;
    
    // 2. Process PCM Data
    const size_t CHUNK_SIZE = 8192;
    std::vector<float> chunkBuffer(CHUNK_SIZE * channels);
    
    struct FloatPeak { float min = 0.0f; float max = 0.0f; };
    std::vector<FloatPeak> tempPeaks(resolution);
    
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

    while (currentWindowIndex < resolution) {
        size_t framesRead = decoder.read_f32(chunkBuffer.data(), CHUNK_SIZE);
        if (framesRead == 0) break;

        for (size_t f = 0; f < framesRead; ++f) {
            float frameMin = 1.0f;
            float frameMax = -1.0f;
            for (unsigned int c = 0; c < channels; ++c) {
                float val = chunkBuffer[f * channels + c];
                if (val < frameMin) frameMin = val;
                if (val > frameMax) frameMax = val;
            }

            currentMin = std::min(currentMin, frameMin);
            currentMax = std::max(currentMax, frameMax);
            
            samplesInCurrentWindow += 1.0;

            if (samplesInCurrentWindow >= samplesPerWindow && currentWindowIndex < resolution) {
                float sMin = apply_gamma(currentMin);
                float sMax = apply_gamma(currentMax);

                tempPeaks[currentWindowIndex] = {sMin, sMax};
                globalMax = std::max({globalMax, std::abs(sMin), std::abs(sMax)});

                currentMin = 0.0f;
                currentMax = 0.0f;
                samplesInCurrentWindow -= samplesPerWindow;
                currentWindowIndex++;
            }
        }
    }

    // 3. Normalize and Quantize
    res.waveform_peaks.assign(resolution, {0, 0});
    if (globalMax > 1e-6f) {
        for (int i = 0; i < resolution; ++i) {
            res.waveform_peaks[i].minPeak = (int16_t)((tempPeaks[i].min / globalMax) * 32767.0f);
            res.waveform_peaks[i].maxPeak = (int16_t)((tempPeaks[i].max / globalMax) * 32767.0f);
        }
    }

    res.success = true;
    CROW_LOG_INFO << "Successfully extracted waveform for " << filepath << " (" << res.format << ")";
    return res;
}

} // namespace services
