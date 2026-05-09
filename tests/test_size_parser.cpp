#include <catch2/catch_test_macros.hpp>
#include "utils/size_parser.h"

TEST_CASE("Size string parsing", "[utils][size_parser]") {
    SECTION("Basic sizes") {
        REQUIRE(utils::parse_size_string("1024") == 1024ULL);
        REQUIRE(utils::parse_size_string("0") == 0ULL);
        REQUIRE(utils::parse_size_string("") == 0ULL);
    }

    SECTION("KiloBytes") {
        REQUIRE(utils::parse_size_string("1K") == 1024ULL);
        REQUIRE(utils::parse_size_string("10KB") == 10240ULL);
        REQUIRE(utils::parse_size_string("10 kb") == 10240ULL);
    }

    SECTION("MegaBytes") {
        REQUIRE(utils::parse_size_string("1M") == 1024ULL * 1024ULL);
        REQUIRE(utils::parse_size_string("500MB") == 500ULL * 1024ULL * 1024ULL);
    }

    SECTION("GigaBytes") {
        REQUIRE(utils::parse_size_string("1G") == 1024ULL * 1024ULL * 1024ULL);
        REQUIRE(utils::parse_size_string("5GB") == 5ULL * 1024ULL * 1024ULL * 1024ULL);
    }

    SECTION("Invalid formats") {
        REQUIRE(utils::parse_size_string("abc") == 0ULL);
        REQUIRE(utils::parse_size_string("G1") == 0ULL);
    }
}
