#include <catch2/catch_test_macros.hpp>
#include "services/media_service.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdint>

// Simple function to write a valid 16-bit PCM WAV file
void generate_test_wav(const std::string& path, float frequency, int duration_ms) {
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

TEST_CASE("MediaService extraction", "[services][media_service]") {
    
    SECTION("Real WAV file processing") {
        std::string test_path = "test_audio.wav";
        generate_test_wav(test_path, 440.0f, 1000); // 1 second of A440

        auto res = services::MediaService::extract_waveform(test_path, 512);
        
        REQUIRE(res.success);
        REQUIRE(res.format == "WAV");
        REQUIRE(std::abs(res.duration_seconds - 1.0f) < 0.01f);
        REQUIRE(res.waveform_data.size() == 512);
        REQUIRE(res.waveform_peaks.size() == 512);
        
        // Since it's a 440Hz sine wave, peaks should be near 1.0 (normalized)
        // and symmetric around the center.
        for (int i = 0; i < 512; ++i) {
            REQUIRE(res.waveform_data[i] > 0.9f);
            
            // Check quantized peaks
            REQUIRE(res.waveform_peaks[i].maxPeak > 29000); // ~0.9 * 32767
            REQUIRE(res.waveform_peaks[i].minPeak < -29000); // ~ -0.9 * 32767
        }

        std::filesystem::remove(test_path);
    }

    SECTION("Non-existent file") {
        auto res = services::MediaService::extract_waveform("non_existent.wav", 100);
        REQUIRE(!res.success);
        REQUIRE(res.error_message == "File not found");
    }

    SECTION("Unsupported format") {
        std::ofstream ofs("test.txt");
        ofs << "not an audio file";
        ofs.close();

        auto res = services::MediaService::extract_waveform("test.txt", 100);
        REQUIRE(!res.success);
        REQUIRE(res.error_message.find("Unsupported file format") != std::string::npos);

        std::filesystem::remove("test.txt");
    }
}
