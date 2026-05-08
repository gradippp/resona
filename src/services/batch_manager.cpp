#include "batch_manager.h"
#include "download_service.h"
#include "database_service.h"
#include "../utils/uuid.h"
#include "crow/logging.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

static std::string escape_string(MYSQL* conn, const std::string& str) {
    std::string escaped(str.length() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, &escaped[0], str.c_str(), str.length());
    escaped.resize(len);
    return escaped;
}

std::string BatchManager::create_batch(const models::CreateBatchRequest& req) {
    std::string id = utils::generate_uuid_v4();
    MYSQL* conn = DatabaseService::get_instance().get_connection();

    json options = {
        {"wait_duration", req.wait_duration},
        {"max_retries", req.max_retries},
        {"max_batch_size", req.max_batch_size},
        {"max_batch_storage", req.max_batch_storage},
        {"allowed_services", req.allowed_services},
        {"delete_after", req.delete_after},
        {"waveform_resolution", req.waveform_resolution}
    };

    std::string query = "INSERT INTO batches (id, status, options) VALUES ('" + 
                        id + "', 'pending', '" + escape_string(conn, options.dump()) + "')";

    if (mysql_query(conn, query.c_str())) {
        CROW_LOG_ERROR << "Failed to create batch: " << mysql_error(conn);
        return "";
    }
    
    CROW_LOG_INFO << "Created new batch: " << id << " (delete_after: " << req.delete_after << ")";
    return id;
}

bool BatchManager::add_task(const std::string& batch_id, const models::AddTaskRequest& req) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    // Check if batch exists and is pending
    std::string check_query = "SELECT status FROM batches WHERE id = '" + batch_id + "'";
    if (mysql_query(conn, check_query.c_str())) return false;
    
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return false;
    
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        CROW_LOG_WARNING << "Attempted to add task to non-existent batch: " << batch_id;
        return false;
    }
    
    std::string status = row[0];
    mysql_free_result(res);
    
    if (status != "pending") {
        CROW_LOG_WARNING << "Attempted to add task to batch that is no longer pending: " << batch_id;
        return false;
    }

    std::string task_id = utils::generate_uuid_v4();
    
    // Handle STORAGE_DIRECTORY env var
    std::filesystem::path dest_path = req.destination_path;
    const char* storage_dir = std::getenv("STORAGE_DIRECTORY");
    if (storage_dir != nullptr) {
        std::filesystem::path base_dir(storage_dir);
        std::string rel_path = req.destination_path;
        if (!rel_path.empty() && (rel_path[0] == '/' || rel_path[0] == '\\')) {
            rel_path = rel_path.substr(1);
        }
        dest_path = base_dir / rel_path;
    }

    std::string insert_query = "INSERT INTO tasks (id, batch_id, file_id, destination_path, status) VALUES ('" +
                               task_id + "', '" + batch_id + "', '" + 
                               escape_string(conn, req.file_id) + "', '" + 
                               escape_string(conn, dest_path.string()) + "', 'pending')";

    if (mysql_query(conn, insert_query.c_str())) {
        CROW_LOG_ERROR << "Failed to add task: " << mysql_error(conn);
        return false;
    }
    
    CROW_LOG_INFO << "Added task " << task_id << " to batch " << batch_id;
    return true;
}

bool BatchManager::start_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    std::string check_query = "SELECT status FROM batches WHERE id = '" + batch_id + "'";
    if (mysql_query(conn, check_query.c_str())) return false;
    
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return false;
    
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return false;
    }
    
    std::string status = row[0];
    mysql_free_result(res);
    
    if (status != "pending") return false;

    std::string update_query = "UPDATE batches SET status = 'processing' WHERE id = '" + batch_id + "'";
    if (mysql_query(conn, update_query.c_str())) return false;

    CROW_LOG_INFO << "Starting batch: " << batch_id;
    
    std::thread([this, batch_id]() {
        MYSQL* conn = DatabaseService::get_instance().get_connection();
        
        // Fetch tasks
        std::string fetch_query = "SELECT id, file_id, destination_path FROM tasks WHERE batch_id = '" + batch_id + "'";
        if (mysql_query(conn, fetch_query.c_str())) return;
        
        MYSQL_RES* res = mysql_store_result(conn);
        if (!res) return;
        
        struct TaskInfo { std::string id, file_id, dest; };
        std::vector<TaskInfo> tasks;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            tasks.push_back({row[0], row[1], row[2]});
        }
        mysql_free_result(res);

        for (const auto& task : tasks) {
            std::string up_status = "UPDATE tasks SET status = 'downloading' WHERE id = '" + task.id + "'";
            mysql_query(conn, up_status.c_str());

            bool success = DownloadService::download_file(task.file_id, task.dest);

            std::string final_status = success ? "success" : "failed";
            std::string local_url = success ? "file://" + escape_string(conn, task.dest) : "";
            
            std::string up_task = "UPDATE tasks SET status = '" + final_status + "', local_url = '" + local_url + "' WHERE id = '" + task.id + "'";
            mysql_query(conn, up_task.c_str());
        }

        std::string final_batch = "UPDATE batches SET status = 'awaiting' WHERE id = '" + batch_id + "'";
        mysql_query(conn, final_batch.c_str());
        CROW_LOG_INFO << "Background processing moved batch to awaiting: " << batch_id;
    }).detach();
    
    return true;
}

bool BatchManager::complete_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    std::string query = "UPDATE batches SET status = 'completed' WHERE id = '" + batch_id + "' AND status = 'awaiting'";
    if (mysql_query(conn, query.c_str())) return false;
    
    if (mysql_affected_rows(conn) == 0) {
        CROW_LOG_WARNING << "Batch " << batch_id << " not found or not in awaiting state";
        return false;
    }

    CROW_LOG_INFO << "Batch " << batch_id << " marked as completed";

    // Trigger cleanup if needed
    auto batch_opt = get_batch(batch_id);
    if (batch_opt) {
        auto delay = parse_duration(batch_opt->options.delete_after);
        if (delay.count() > 0) {
            std::thread([batch_id, delay, this]() {
                std::this_thread::sleep_for(delay);
                auto b = get_batch(batch_id);
                if (b) {
                    for (const auto& t : b->tasks) {
                        if (t.status == "success") {
                            std::filesystem::remove(t.destination_path);
                        }
                    }
                }
            }).detach();
        }
    }

    return true;
}

std::optional<models::Batch> BatchManager::get_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    std::string query = "SELECT status, options FROM batches WHERE id = '" + batch_id + "'";
    if (mysql_query(conn, query.c_str())) return std::nullopt;
    
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return std::nullopt;
    
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return std::nullopt;
    }
    
    models::Batch b;
    b.id = batch_id;
    b.status = row[0];
    
    try {
        json options = json::parse(row[1]);
        b.options.wait_duration = options.value("wait_duration", 0);
        b.options.max_retries = options.value("max_retries", 0);
        b.options.max_batch_size = options.value("max_batch_size", 0);
        b.options.max_batch_storage = options.value("max_batch_storage", "");
        b.options.allowed_services = options.value("allowed_services", std::vector<std::string>{});
        b.options.delete_after = options.value("delete_after", "");
        b.options.waveform_resolution = options.value("waveform_resolution", 256);
    } catch (...) {}
    
    mysql_free_result(res);

    // Fetch tasks
    std::string task_query = "SELECT id, file_id, destination_path, status, local_url FROM tasks WHERE batch_id = '" + batch_id + "'";
    if (!mysql_query(conn, task_query.c_str())) {
        MYSQL_RES* task_res = mysql_store_result(conn);
        if (task_res) {
            while (MYSQL_ROW task_row = mysql_fetch_row(task_res)) {
                models::Task t;
                t.id = task_row[0];
                t.file_id = task_row[1];
                t.destination_path = task_row[2];
                t.status = task_row[3];
                t.local_url = task_row[4] ? task_row[4] : "";
                b.tasks.push_back(t);
            }
            mysql_free_result(task_res);
        }
    }

    return b;
}

} // namespace services
