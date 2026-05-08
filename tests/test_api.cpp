#include <catch2/catch_test_macros.hpp>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "routes/routes.h"
#include "crow.h"
#include <thread>
#include <chrono>

using json = nlohmann::json;

// Fixture to start the Crow server before tests and stop it after
struct ServerFixture {
    crow::SimpleApp app;
    std::thread server_thread;

    ServerFixture() {
        routes::setup(app);
        app.loglevel(crow::LogLevel::Warning);
        
        server_thread = std::thread([this]() {
            app.port(8081).run();
        });
        
        // Give the server a moment to bind to the port
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    ~ServerFixture() {
        app.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
};

TEST_CASE_METHOD(ServerFixture, "API Integration Tests", "[api]") {
    
    SECTION("GET /v1/version returns valid version info") {
        auto response = cpr::Get(cpr::Url{"http://localhost:8081/v1/version"});
        
        REQUIRE(response.status_code == 200);
        
        auto j = json::parse(response.text);
        REQUIRE(j.contains("version"));
        REQUIRE(j["description"] == "Strata");
    }

    SECTION("Complete Batch Lifecycle") {
        // 1. Create a Batch
        auto create_res = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"wait_duration\": 1000, \"max_retries\": 2, \"max_batch_size\": 10, \"max_batch_storage\": \"1G\", \"allowed_services\": [], \"delete_after\": \"\", \"waveform_resolution\": 512}"}
        );
        
        REQUIRE(create_res.status_code == 200);
        auto create_json = json::parse(create_res.text);
        REQUIRE(create_json.contains("batch_id"));
        std::string batch_id = create_json["batch_id"];
        REQUIRE(create_json["status"] == "pending");

        // 2. Add a Task to the Batch
        auto add_res = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/" + batch_id},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"file_id\": \"http://localhost:8081/v1/version\", \"destination_path\": \"test_api_dl.json\"}"}
        );
        
        REQUIRE(add_res.status_code == 202);
        
        // 3. Start the Batch
        auto start_res = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/" + batch_id + "/start"}
        );
        
        REQUIRE(start_res.status_code == 200);

        // Give the background thread time to "process" (real download now)
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // 4. Verify the Batch Status
        auto status_res = cpr::Get(
            cpr::Url{"http://localhost:8081/v1/batch/" + batch_id}
        );
        
        REQUIRE(status_res.status_code == 200);
        auto status_json = json::parse(status_res.text);
        
        REQUIRE(status_json["id"] == batch_id);
        REQUIRE(status_json["status"] == "completed"); // Currently mocked to complete immediately
        REQUIRE(status_json["options"]["wait_duration"] == 1000);
        REQUIRE(status_json["options"]["max_retries"] == 2);
        REQUIRE(status_json["options"]["waveform_resolution"] == 512);
        
        REQUIRE(status_json["tasks"].size() == 1);
        REQUIRE(status_json["tasks"][0]["file_id"] == "http://localhost:8081/v1/version");
        REQUIRE(status_json["tasks"][0]["status"] == "success");
    }

    SECTION("Adding a task to a non-existent batch fails") {
        auto add_res = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/invalid-batch-id"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"file_id\": \"f1\", \"destination_path\": \"/\"}"}
        );
        
        REQUIRE(add_res.status_code == 404);
        auto j = json::parse(add_res.text);
        REQUIRE(j.contains("error"));
    }
}
