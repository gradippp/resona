#pragma once
#include <string>
#include <fstream>
#include <cmath>
#include <cstdint>

namespace test_utils {

const std::string TEST_SERVER_URL = "http://localhost:8081";

/**
 * Generates a valid 16-bit PCM WAV file for testing.
 */
inline void generate_test_wav(const std::string& path, float frequency = 440.0f, int duration_ms = 1000) {
    std::ofstream file(path, std::ios::binary);
    
    int sample_rate = 44100;
    int num_samples = (sample_rate * duration_ms) / 1000;
    
    // RIFF header
    file << "RIFF";
    uint32_t file_size = 36 + num_samples * 2;
    file.write(reinterpret_cast<char*>(&file_size), 4);
    file << "WAVE";
    
    // fmt subchunk
    file << "fmt ";
    uint32_t fmt_size = 16;
    file.write(reinterpret_cast<char*>(&fmt_size), 4);
    uint16_t audio_format = 1; // PCM
    uint16_t num_channels = 1;
    file.write(reinterpret_cast<char*>(&audio_format), 2);
    file.write(reinterpret_cast<char*>(&num_channels), 2);
    file.write(reinterpret_cast<char*>(&sample_rate), 4);
    uint32_t byte_rate = sample_rate * 2;
    file.write(reinterpret_cast<char*>(&byte_rate), 4);
    uint16_t block_align = 2;
    uint16_t bits_per_sample = 16;
    file.write(reinterpret_cast<char*>(&block_align), 2);
    file.write(reinterpret_cast<char*>(&bits_per_sample), 2);
    
    // data subchunk
    file << "data";
    uint32_t data_size = num_samples * 2;
    file.write(reinterpret_cast<char*>(&data_size), 4);
    
    for (int i = 0; i < num_samples; ++i) {
        float t = (float)i / sample_rate;
        int16_t sample = (int16_t)(sin(2.0f * 3.14159f * frequency * t) * 32767.0f);
        file.write(reinterpret_cast<char*>(&sample), 2);
    }
    
    file.close();
}

} // namespace test_utils
