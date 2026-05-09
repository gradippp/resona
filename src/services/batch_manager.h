#pragma once
#include "../models/batch.h"
#include <unordered_map>
#include <string>
#include <mutex>
#include <optional>

namespace services {

class BatchManager {
public:
    static BatchManager& get_instance() {
        static BatchManager instance;
        return instance;
    }

    std::string create_batch(const models::CreateBatchRequest& req);
    bool add_task(const std::string& batch_id, const models::AddTaskRequest& req);
    bool start_batch(const std::string& batch_id);
    bool complete_batch(const std::string& batch_id);
    std::optional<models::Batch> get_batch(const std::string& batch_id);
    std::optional<models::Task> get_ingested_task(const std::string& task_id);

private:
    BatchManager() = default;
    std::mutex db_mutex_;
};

} // namespace services
