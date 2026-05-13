#include <catch2/catch_test_macros.hpp>
#include "utils/time_utils.h"
#include <chrono>
#include <thread>

TEST_CASE("Time utilities - Duration parsing", "[utils][time]") {
    SECTION("Basic durations") {
        REQUIRE(utils::parse_duration_ms("500ms") == std::chrono::milliseconds(500));
        REQUIRE(utils::parse_duration_ms("5s") == std::chrono::seconds(5));
        REQUIRE(utils::parse_duration_ms("2m") == std::chrono::minutes(2));
        REQUIRE(utils::parse_duration_ms("1h") == std::chrono::hours(1));
    }

    SECTION("Case insensitivity") {
        REQUIRE(utils::parse_duration_ms("5S") == std::chrono::seconds(5));
        REQUIRE(utils::parse_duration_ms("10M") == std::chrono::minutes(10));
        REQUIRE(utils::parse_duration_ms("1H") == std::chrono::hours(1));
    }

    SECTION("Invalid inputs") {
        REQUIRE(utils::parse_duration_ms("").count() == 0);
        REQUIRE(utils::parse_duration_ms("abc").count() == 0);
        REQUIRE(utils::parse_duration_ms("5x").count() == 0);
    }

    SECTION("Seconds conversion") {
        REQUIRE(utils::parse_duration_s("1h").count() == 3600);
        REQUIRE(utils::parse_duration_s("90s").count() == 90);
    }
}

TEST_CASE("Time utilities - HTTP Date parsing", "[utils][time]") {
    SECTION("Seconds as string") {
        REQUIRE(utils::parse_http_date_to_seconds("120") == 120);
    }

    SECTION("Invalid or empty") {
        REQUIRE(utils::parse_http_date_to_seconds("", 45) == 45);
        REQUIRE(utils::parse_http_date_to_seconds("not a date", 30) == 30);
    }

    SECTION("HTTP Date format") {
        // This is a bit tricky to test exactly because it depends on 'now'
        // But we can test if it parses a future date correctly.
        time_t future = time(nullptr) + 3600;
        std::tm* tm = gmtime(&future);
        char buf[100];
        strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tm);
        
        int diff = utils::parse_http_date_to_seconds(buf);
        // Should be around 3600, allow some leeway for execution time
        REQUIRE(diff >= 3590);
        REQUIRE(diff <= 3600);
    }
}
