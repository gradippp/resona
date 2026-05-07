#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "utils/uuid.h"
#include <regex>
#include <set>

TEST_CASE("UUID v4 generation", "[utils][uuid]") {
    SECTION("Correct format") {
        std::string uuid = utils::generate_uuid_v4();
        // Regex for UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx where y is [89ab]
        std::regex pattern("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
        REQUIRE(std::regex_match(uuid, pattern));
    }

    SECTION("Uniqueness") {
        std::set<std::string> uuids;
        for (int i = 0; i < 100; ++i) {
            std::string uuid = utils::generate_uuid_v4();
            REQUIRE(uuids.find(uuid) == uuids.end());
            uuids.insert(uuid);
        }
    }
}
