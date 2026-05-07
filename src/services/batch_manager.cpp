#include "batch_manager.h"
#include "download_service.h"
#include "../utils/uuid.h"
#include "crow/logging.h"
#include <thread>

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
    
    // Launch background thread for processing
    std::thread([this, batch_id]() {
        CROW_LOG_INFO << "Background processing started for batch: " << batch_id;
        
        std::vector<models::Task> tasks_to_process;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_to_process = batches_[batch_id].tasks;
        }

        for (auto& task : tasks_to_process) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& t : batches_[batch_id].tasks) {
                    if (t.id == task.id) {
                        t.status = "downloading";
                        break;
                    }
                }
            }

            bool success = DownloadService::download_file(task.file_id, task.destination_path);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& t : batches_[batch_id].tasks) {
                    if (t.id == task.id) {
                        t.status = success ? "success" : "failed";
                        if (success) {
                            t.local_url = "file://" + task.destination_path;
                        }
                        break;
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            batches_[batch_id].status = "completed";
        }
        CROW_LOG_INFO << "Background processing completed for batch: " << batch_id;
    }).detach(); // Detach to let it run independently
    
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
