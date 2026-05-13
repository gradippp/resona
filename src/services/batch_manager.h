#pragma once
#include "../models/batch.h"
#include <unordered_map>
#include <string>
#include <mutex>
#include <optional>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <vector>

namespace services {

class BatchManager {
public:
    static BatchManager& get_instance() {
        static BatchManager instance;
        return instance;
    }

    ~BatchManager();

    std::string create_batch(const models::CreateBatchRequest& req);
    std::optional<std::string> add_task(const std::string& batch_id, const models::AddTaskRequest& req);
    bool start_batch(const std::string& batch_id);
    bool complete_batch(const std::string& batch_id);
    void start_monitors();
    void recover_stuck_tasks();
    std::optional<models::Batch> get_batch(const std::string& batch_id);
    std::optional<models::Task> get_ingested_task(const std::string& task_id);
    std::string get_storage_directory() const { return storage_dir_; }

    /**
     * Gracefully stops all background monitors.
     */
    void stop();

private:
    BatchManager();
    nlohmann::json parse_batch_options(const std::string& options_str);
    
    std::mutex db_mutex_;
    std::string storage_dir_;

    // Thread management
    std::atomic<bool> stop_flag_;
    std::vector<std::thread> monitor_threads_;
    std::mutex thread_mutex_;
    
    // Condition variables for efficient notification
    std::condition_variable task_cv_;
    std::mutex cv_mutex_;
};

} // namespace services
