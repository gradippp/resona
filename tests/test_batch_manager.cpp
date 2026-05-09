#include <catch2/catch_test_macros.hpp>
#include "services/batch_manager.h"
#include "services/database_service.h"
#include "models/batch.h"
#include <thread>
#include <chrono>
#include <iostream>

TEST_CASE("BatchManager functionality", "[services][batch_manager]") {
    auto& manager = services::BatchManager::get_instance();

    SECTION("Create Batch") {
        models::CreateBatchRequest req;
        req.wait_duration = "3s";
        req.delete_after = "24H";
        
        std::string batch_id = manager.create_batch(req);
        REQUIRE(!batch_id.empty());
        
        auto batch = manager.get_batch(batch_id);
        REQUIRE(batch.has_value());
        REQUIRE(batch->id == batch_id);
        REQUIRE(batch->options.wait_duration == "3s");
        REQUIRE(batch->options.delete_after == "24H");
        REQUIRE(batch->status == "pending");
    }

    SECTION("Max Batch Size Enforcement") {
        models::CreateBatchRequest req;
        req.max_batch_size = 2;
        req.delete_after = "24H";
        std::string batch_id = manager.create_batch(req);

        models::AddTaskRequest task;
        task.file_id = "f1";
        
        REQUIRE(manager.add_task(batch_id, task));
        
        task.file_id = "f2";
        REQUIRE(manager.add_task(batch_id, task));

        task.file_id = "f3";
        REQUIRE(!manager.add_task(batch_id, task)); // Should fail
    }

    SECTION("Start and Complete Batch") {
        models::CreateBatchRequest req;
        req.wait_duration = "1s"; // 1 second wait
        req.max_retries = 0;     // No retries for fast fail
        req.delete_after = "24H";
        std::string batch_id = manager.create_batch(req);
        
        bool success = manager.start_batch(batch_id);
        REQUIRE(success);
        
        // Batch should be awaiting
        REQUIRE(manager.get_batch(batch_id)->status == "awaiting");

        models::AddTaskRequest task_req;
        task_req.file_id = "http://127.0.0.1:1"; 
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

    SECTION("Allowed Services Enforcement") {
        models::CreateBatchRequest req;
        req.allowed_services = {"DROPBOX"};
        req.delete_after = "24H";
        std::string batch_id = manager.create_batch(req);

        // Dropbox URL should succeed
        models::AddTaskRequest task1;
        task1.file_id = "https://www.dropbox.com/s/123/file.wav?dl=0";
        REQUIRE(manager.add_task(batch_id, task1));

        // Google Drive URL should fail
        models::AddTaskRequest task2;
        task2.file_id = "https://drive.google.com/file/d/abc/view";
        REQUIRE(!manager.add_task(batch_id, task2));

        // Any URL should fail if not dropbox
        models::AddTaskRequest task3;
        task3.file_id = "https://example.com/file.wav";
        REQUIRE(!manager.add_task(batch_id, task3));

        // New batch with multiple allowed
        req.allowed_services = {"DROPBOX", "GOOGLE_DRIVE"};
        req.delete_after = "24H";
        std::string batch_id2 = manager.create_batch(req);
        REQUIRE(manager.add_task(batch_id2, task1));
        REQUIRE(manager.add_task(batch_id2, task2));

        // Test DIRECT
        req.allowed_services = {"DIRECT"};
        req.delete_after = "24H";
        std::string batch_id3 = manager.create_batch(req);
        REQUIRE(!manager.add_task(batch_id3, task1)); // Dropbox blocked
        
        models::AddTaskRequest task4;
        task4.file_id = "https://example.com/direct.wav";
        REQUIRE(manager.add_task(batch_id3, task4)); // Direct allowed

        req.allowed_services = {"DIRECT", "DROPBOX"};
        req.delete_after = "24H";
        std::string batch_id4 = manager.create_batch(req);
        REQUIRE(manager.add_task(batch_id4, task1)); // Dropbox allowed
        REQUIRE(manager.add_task(batch_id4, task4)); // Direct allowed
        REQUIRE(!manager.add_task(batch_id4, task2)); // GDrive blocked
    }
}
