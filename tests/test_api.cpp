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
            cpr::Body{"{\"wait_duration\": \"2s\", \"max_retries\": 2, \"max_batch_size\": 10, \"max_batch_storage\": \"1G\", \"allowed_services\": [], \"allowed_content_types\": [], \"delete_after\": \"24H\", \"waveform_resolution\": 512, \"transcode_formats\": []}"}
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
            cpr::Body{"{\"file_id\": \"http://localhost:8081/v1/version\"}"}
        );
        REQUIRE(add_res1.status_code == 202);
        auto add_json1 = json::parse(add_res1.text);
        REQUIRE(add_json1.contains("id"));
        REQUIRE_FALSE(add_json1.contains("batch_id"));
        std::string task_id1 = add_json1["id"];
        REQUIRE(!task_id1.empty());

        // Wait for processing (wait_duration 2s + some buffer)
        std::this_thread::sleep_for(std::chrono::milliseconds(6000));

        // 4. Verify first task succeeded and batch is still awaiting
        auto status_res1 = cpr::Get(cpr::Url{"http://localhost:8081/v1/batch/" + batch_id});
        auto status_json1 = json::parse(status_res1.text);
        REQUIRE(status_json1["status"] == "awaiting");
        REQUIRE(status_json1["tasks"].size() == 1);
        REQUIRE(status_json1["tasks"][0]["id"] == task_id1);
        REQUIRE(status_json1["tasks"][0]["status"] == "success");
        
        // VERIFY SIMPLIFICATION: Task in batch response should only have id and status
        REQUIRE(status_json1["tasks"][0].size() == 2);
        REQUIRE(status_json1["tasks"][0].contains("id"));
        REQUIRE(status_json1["tasks"][0].contains("status"));
        REQUIRE_FALSE(status_json1["tasks"][0].contains("file_id"));
        REQUIRE_FALSE(status_json1["tasks"][0].contains("destination_path"));

        // 5. Add second Task
        auto add_res2 = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/" + batch_id},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"file_id\": \"http://localhost:8081/v1/version\"}"}
        );
        REQUIRE(add_res2.status_code == 202);
        auto add_json2 = json::parse(add_res2.text);
        REQUIRE(add_json2.contains("id"));
        REQUIRE_FALSE(add_json2.contains("batch_id"));
        std::string task_id2 = add_json2["id"];
        REQUIRE(task_id1 != task_id2);

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

        // 7. Verify /v1/ingested/{task_id}
        std::string task_id = status_json1["tasks"][0]["id"];
        auto ingested_res = cpr::Get(cpr::Url{"http://localhost:8081/v1/ingested/" + task_id});
        REQUIRE(ingested_res.status_code == 200);
        auto ingested_json = json::parse(ingested_res.text);
        
        REQUIRE(ingested_json["id"] == task_id);
        REQUIRE(ingested_json.contains("metadata"));
        REQUIRE(ingested_json.contains("local_urls"));
        REQUIRE(ingested_json.contains("stream_urls"));
        REQUIRE(ingested_json["local_urls"].is_array());
        REQUIRE(ingested_json["stream_urls"].is_array());
        REQUIRE_FALSE(ingested_json.contains("destination_path"));
        REQUIRE_FALSE(ingested_json.contains("content_type"));
        REQUIRE_FALSE(ingested_json.contains("local_url"));
        // waveform_resolution will be 0 here because it's not a real media file

        // 8. Test /v1/ingested/{task_id}/stream
        // Full file stream
        auto stream_res = cpr::Get(cpr::Url{"http://localhost:8081/v1/ingested/" + task_id + "/stream"});
        REQUIRE(stream_res.status_code == 200);
        REQUIRE(stream_res.header["Accept-Ranges"] == "bytes");
        REQUIRE(!stream_res.text.empty());

        // Partial range stream (first 10 bytes)
        auto range_res = cpr::Get(
            cpr::Url{"http://localhost:8081/v1/ingested/" + task_id + "/stream"},
            cpr::Header{{"Range", "bytes=0-9"}}
        );
        REQUIRE(range_res.status_code == 206);
        REQUIRE(range_res.header["Accept-Ranges"] == "bytes");
        REQUIRE(range_res.header.count("Content-Range") > 0);
        REQUIRE(range_res.text.length() == 10);
    }

    SECTION("Adding a task to a non-existent batch fails") {
        auto add_res = cpr::Post(
            cpr::Url{"http://localhost:8081/v1/batch/invalid-batch-id"},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{"{\"file_id\": \"f1\"}"}
        );
        
        REQUIRE(add_res.status_code == 404);
        auto j = json::parse(add_res.text);
        REQUIRE(j["title"] == "Not Found");
        REQUIRE(j["status"] == 404);
        REQUIRE(j["detail"] == "Batch not found or already started");
        REQUIRE(j.contains("type"));
    }

    SECTION("404 Inconsistency Check (Reproduction)") {
        // Reported UUID that gives raw 404
        std::string reported_uuid = "133d1756-1873-4d0d-bf78-d3e521d0be63";
        auto res1 = cpr::Get(cpr::Url{"http://localhost:8081/v1/ingested/" + reported_uuid + "/stream"});
        
        // This should now return a JSON 404
        REQUIRE(res1.status_code == 404);
        CHECK(res1.header["Content-Type"].find("application/json") != std::string::npos);
        auto j1 = json::parse(res1.text);
        REQUIRE(j1["detail"] == "Ingested task not found");
        
        // Very long ID that gives JSON 404
        std::string long_id = "133d1756-1873-4d0d-bf78-d3e521d0be6adagadg3";
        auto res2 = cpr::Get(cpr::Url{"http://localhost:8081/v1/ingested/" + long_id + "/stream"});
        
        REQUIRE(res2.status_code == 404);
        auto j2 = json::parse(res2.text);
        REQUIRE(j2["detail"] == "Ingested task not found");
    }
}
