#include <catch2/catch_test_macros.hpp>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "routes/routes.h"
#include "services/database_service.h"
#include "crow.h"
#include <thread>
#include <chrono>

using json = nlohmann::json;

// Fixture to start the Crow server before tests and stop it after
struct ServerFixture {
    crow::SimpleApp app;
    std::thread server_thread;

    ServerFixture() {
        // Initialize database for testing
        try {
            auto& db = services::DatabaseService::get_instance();
            try {
                db.initialize("127.0.0.1", 3307, "root", "root", "resona");
            } catch (...) {
                db.initialize("127.0.0.1", 3307, "root", "root", "");
                mysql_query(db.get_connection(), "CREATE DATABASE IF NOT EXISTS resona");
                db.initialize("127.0.0.1", 3307, "root", "root", "resona");
            }
            db.initialize_schema();
        } catch (...) {}

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
        REQUIRE(j["description"] == "Resona");
    }

    SECTION("Long-lived Batch Lifecycle (Awaiting)") {
        // 1. Create a Batch with a short wait_duration
        auto create_res = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"wait_duration\": 2000, \"max_retries\": 2, \"max_batch_size\": 10, \"max_batch_storage\": \"1G\", \"allowed_services\": [], \"delete_after\": \"24H\", \"waveform_resolution\": 512}"}
        );
        
        REQUIRE(create_res.status_code == 200);
        auto create_json = json::parse(create_res.text);
        std::string batch_id = create_json["batch_id"];

        // 2. Start the Batch immediately (it moves to awaiting)
        auto start_res = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/" + batch_id + "/start"}
        );
        REQUIRE(start_res.status_code == 200);

        // 3. Add first Task
        auto add_res1 = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/" + batch_id},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"file_id\": \"http://localhost:8081/v1/version\", \"destination_path\": \"test_awaiting_1.json\"}"}
        );
        REQUIRE(add_res1.status_code == 202);

        // Wait for processing (wait_duration 2s + some buffer)
        std::this_thread::sleep_for(std::chrono::milliseconds(6000));

        // 4. Verify first task succeeded and batch is still awaiting
        auto status_res1 = cpr::Get(cpr::Url{"http://localhost:8081/v1/batch/" + batch_id});
        auto status_json1 = json::parse(status_res1.text);
        REQUIRE(status_json1["status"] == "awaiting");
        REQUIRE(status_json1["tasks"].size() == 1);
        REQUIRE(status_json1["tasks"][0]["status"] == "success");

        // 5. Add second Task
        auto add_res2 = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/" + batch_id},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"file_id\": \"http://localhost:8081/v1/version\", \"destination_path\": \"test_awaiting_2.json\"}"}
        );
        REQUIRE(add_res2.status_code == 202);

        // Wait for second task
        std::this_thread::sleep_for(std::chrono::milliseconds(6000));

        // 6. Verify second task succeeded
        auto status_res2 = cpr::Get(cpr::Url{"http://localhost:8081/v1/batch/" + batch_id});
        auto status_json2 = json::parse(status_res2.text);
        REQUIRE(status_json2["tasks"].size() == 2);
        REQUIRE(status_json2["tasks"][1]["status"] == "success");

        // 7. Complete the batch
        auto complete_res = cpr::Post(cpr::Url{"http://localhost:8081/v1/batch/" + batch_id + "/complete"});
        REQUIRE(complete_res.status_code == 200);

        auto final_status_res = cpr::Get(cpr::Url{"http://localhost:8081/v1/batch/" + batch_id});
        REQUIRE(json::parse(final_status_res.text)["status"] == "completed");
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
