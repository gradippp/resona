#include "batch_manager.h"
#include "../utils/uuid.h"
#include "crow/logging.h"

namespace services {

std::string BatchManager::create_batch(const models::CreateBatchRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = utils::generate_uuid_v4();
    models::Batch b;
    b.id = id;
    b.status = "pending";
    b.options = req;
    batches_[id] = b;
    
    CROW_LOG_INFO << "Created new batch: " << id;
    return id;
}

bool BatchManager::add_task(const std::string& batch_id, const models::AddTaskRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = batches_.find(batch_id);
    if (it == batches_.end()) {
        CROW_LOG_WARNING << "Attempted to add task to non-existent batch: " << batch_id;
        return false;
    }
    
    if (it->second.status != "pending") {
        CROW_LOG_WARNING << "Attempted to add task to batch that is no longer pending: " << batch_id;
        return false;
    }

    models::Task t;
    t.id = utils::generate_uuid_v4();
    t.file_id = req.file_id;
    t.destination_path = req.destination_path;
    it->second.tasks.push_back(t);
    
    CROW_LOG_INFO << "Added task " << t.id << " (file: " << t.file_id << ") to batch " << batch_id;
    return true;
}

bool BatchManager::start_batch(const std::string& batch_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = batches_.find(batch_id);
    if (it == batches_.end()) {
        CROW_LOG_WARNING << "Attempted to start non-existent batch: " << batch_id;
        return false;
    }
    
    if (it->second.status != "pending") {
        CROW_LOG_WARNING << "Attempted to start batch that is already " << it->second.status << ": " << batch_id;
        return false;
    }

    CROW_LOG_INFO << "Starting batch: " << batch_id;
    it->second.status = "processing";
    
    // Mock processing: mark as completed immediately
    it->second.status = "completed";
    for (auto& task : it->second.tasks) {
        task.status = "success";
        task.local_url = "/downloads/" + task.id + ".zip";
    }
    
    CROW_LOG_INFO << "Batch completed successfully: " << batch_id;
    return true;
}

std::optional<models::Batch> BatchManager::get_batch(const std::string& batch_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = batches_.find(batch_id);
    if (it != batches_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace services
