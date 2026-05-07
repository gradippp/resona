#include <catch2/catch_test_macros.hpp>
#include "services/batch_manager.h"
#include "models/batch.h"

TEST_CASE("BatchManager functionality", "[services][batch_manager]") {
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

    SECTION("Start Batch") {
        models::CreateBatchRequest req;
        std::string batch_id = manager.create_batch(req);
        
        models::AddTaskRequest task_req;
        task_req.file_id = "test-file-2";
        manager.add_task(batch_id, task_req);
        
        bool success = manager.start_batch(batch_id);
        REQUIRE(success);
        
        auto batch = manager.get_batch(batch_id);
        REQUIRE(batch->status == "completed");
        REQUIRE(batch->tasks[0].status == "success");
        
        // Cannot add task after started
        bool add_fail = manager.add_task(batch_id, task_req);
        REQUIRE(!add_fail);
    }
}
