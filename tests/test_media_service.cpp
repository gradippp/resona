#include <catch2/catch_test_macros.hpp>
#include "services/media_service.h"
#include <filesystem>
#include <fstream>

TEST_CASE("MediaService extraction", "[services][media_service]") {
    // Note: This test requires a valid audio file. 
    // In a real environment, we'd bundle a small test.wav.
    // For now, we'll verify the error handling and basic logic if possible.
    
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
