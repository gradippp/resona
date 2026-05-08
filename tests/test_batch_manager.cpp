#include <catch2/catch_test_macros.hpp>
#include "services/batch_manager.h"
#include "services/database_service.h"
#include "models/batch.h"
#include <thread>
#include <chrono>

TEST_CASE("BatchManager functionality", "[services][batch_manager]") {
    // Initialize database for testing
    static bool db_initialized = false;
    if (!db_initialized) {
        try {
            services::DatabaseService::get_instance().initialize("localhost", 3307, "root", "root", "strata");
            services::DatabaseService::get_instance().initialize_schema();
            db_initialized = true;
        } catch (...) {
            // If it fails, tests will fail anyway with the connection error
        }
    }

    auto& manager = services::BatchManager::get_instance();

    SECTION("Create Batch") {
        models::CreateBatchRequest req;
        req.wait_duration = 3000;
        
        std::string batch_id = manager.create_batch(req);
        REQUIRE(!batch_id.empty());
        
        auto batch = manager.get_batch(batch_id);
        REQUIRE(batch.has_value());
        REQUIRE(batch->id == batch_id);
        REQUIRE(batch->options.wait_duration == 3000);
        REQUIRE(batch->status == "pending");
    }

    SECTION("Add Tasks") {
        models::CreateBatchRequest req;
        std::string batch_id = manager.create_batch(req);
        
        models::AddTaskRequest task_req;
        task_req.file_id = "test-file-1";
        task_req.destination_path = "/tmp/test1";
        
        bool success = manager.add_task(batch_id, task_req);
        REQUIRE(success);
        
        auto batch = manager.get_batch(batch_id);
        REQUIRE(batch->tasks.size() == 1);
        REQUIRE(batch->tasks[0].file_id == "test-file-1");
    }

    SECTION("Start and Complete Batch") {
        models::CreateBatchRequest req;
        req.wait_duration = 1000; // 1 second wait
        req.max_retries = 0;     // No retries for fast fail
        std::string batch_id = manager.create_batch(req);
        
        bool success = manager.start_batch(batch_id);
        REQUIRE(success);
        
        // Batch should be awaiting
        REQUIRE(manager.get_batch(batch_id)->status == "awaiting");

        models::AddTaskRequest task_req;
        task_req.file_id = "http://127.0.0.1:1"; 
        task_req.destination_path = "test_batch_2.bin";
        manager.add_task(batch_id, task_req);
        
        // Wait for background processing (wait_duration 1s + buffer + download time + retries)
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));

        auto batch = manager.get_batch(batch_id);
        REQUIRE(batch->status == "awaiting");
        REQUIRE(batch->tasks.size() == 1);
        REQUIRE(batch->tasks[0].status == "failed"); // Expected fail on port 1
        
        // Mark as completed
        bool complete_success = manager.complete_batch(batch_id);
        REQUIRE(complete_success);
        
        batch = manager.get_batch(batch_id);
        REQUIRE(batch->status == "completed");
    }
}
