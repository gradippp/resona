#include <catch2/catch_test_macros.hpp>
#include "services/media_service.h"
#include "test_utils.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdint>

TEST_CASE("MediaService extraction", "[services][media_service]") {
    
    SECTION("Real WAV file processing") {
        std::string test_path = "test_audio.wav";
        test_utils::generate_test_wav(test_path, 440.0f, 1000); // 1 second of A440

        auto res = services::MediaService::extract_waveform(test_path, 512);
        
        REQUIRE(res.success);
        REQUIRE(res.format == "WAV");
        REQUIRE(std::abs(res.duration_seconds - 1.0f) < 0.01f);
        REQUIRE(res.waveform_peaks.size() == 512);

        // Since it's a 440Hz sine wave, peaks should be near 1.0 (normalized)
        // and symmetric around the center.
        for (int i = 0; i < 512; ++i) {
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

    SECTION("Transcoding WAV to WAV (PCM)") {
        std::string input_path = "test_transcode_input.wav";
        std::string output_path = "test_transcode_output_pcm.wav";
        test_utils::generate_test_wav(input_path, 440.0f, 1000); // 1 second of A440

        bool success = services::MediaService::transcode_audio(input_path, output_path, "wav");
        
        REQUIRE(success);
        REQUIRE(std::filesystem::exists(output_path));
        REQUIRE(std::filesystem::file_size(output_path) > 0);

        // Verify it's actually a WAV by trying to extract its waveform
        auto res = services::MediaService::extract_waveform(output_path, 100);
        REQUIRE(res.success);
        REQUIRE(res.format == "WAV");

        std::filesystem::remove(input_path);
        std::filesystem::remove(output_path);
    }
}
