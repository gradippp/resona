#include "batch_manager.h"
#include "download_service.h"
#include "../utils/uuid.h"
#include "crow/logging.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>

namespace services {

static std::chrono::seconds parse_duration(const std::string& duration_str) {
    if (duration_str.empty()) return std::chrono::seconds(0);
    try {
        size_t pos = 0;
        int value = std::stoi(duration_str, &pos);
        std::string unit = duration_str.substr(pos);
        if (unit == "H" || unit == "h") return std::chrono::hours(value);
        if (unit == "m") return std::chrono::minutes(value);
        if (unit == "s") return std::chrono::seconds(value);
    } catch (...) {}
    return std::chrono::seconds(0);
}

std::string BatchManager::create_batch(const models::CreateBatchRequest& req) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = utils::generate_uuid_v4();
    models::Batch b;
    b.id = id;
    b.status = "pending";
    b.options = req;
    batches_[id] = b;
    
    CROW_LOG_INFO << "Created new batch: " << id << " (delete_after: " << req.delete_after << ")";
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

    // Handle STORAGE_DIRECTORY env var
    std::filesystem::path dest_path = req.destination_path;
    const char* storage_dir = std::getenv("STORAGE_DIRECTORY");
    if (storage_dir != nullptr) {
        std::filesystem::path base_dir(storage_dir);
        std::string rel_path = req.destination_path;
        // Strip leading slashes to keep it relative to storage_dir
        if (!rel_path.empty() && (rel_path[0] == '/' || rel_path[0] == '\\')) {
            rel_path = rel_path.substr(1);
        }
        dest_path = base_dir / rel_path;
    }
    t.destination_path = dest_path.string();

    it->second.tasks.push_back(t);
    
    CROW_LOG_INFO << "Added task " << t.id << " (file: " << t.file_id << ") to batch " << batch_id << " -> " << t.destination_path;
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
        std::string delete_after_str;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_to_process = batches_[batch_id].tasks;
            delete_after_str = batches_[batch_id].options.delete_after;
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

        // Auto-cleanup logic
        auto delay = parse_duration(delete_after_str);
        if (delay.count() > 0) {
            CROW_LOG_INFO << "Scheduled cleanup for batch " << batch_id << " in " << delete_after_str;
            std::thread([this, batch_id, delay]() {
                std::this_thread::sleep_for(delay);
                CROW_LOG_INFO << "Running scheduled cleanup for batch: " << batch_id;
                
                std::vector<std::string> paths_to_delete;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (batches_.count(batch_id)) {
                        for (const auto& task : batches_[batch_id].tasks) {
                            if (task.status == "success") {
                                paths_to_delete.push_back(task.destination_path);
                            }
                        }
                    }
                }

                for (const auto& path : paths_to_delete) {
                    std::error_code ec;
                    if (std::filesystem::remove(path, ec)) {
                        CROW_LOG_INFO << "Deleted file: " << path;
                    } else if (ec) {
                        CROW_LOG_WARNING << "Failed to delete file: " << path << " (" << ec.message() << ")";
                    }
                }
            }).detach();
        }
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
