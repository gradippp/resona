#include "batch_manager.h"
#include "download_service.h"
#include "media_service.h"
#include "database_service.h"
#include "../utils/uuid.h"
#include "../utils/size_parser.h"
#include "crow/logging.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace services {

static std::chrono::milliseconds parse_duration_ms(const std::string& duration_str) {
    if (duration_str.empty()) return std::chrono::milliseconds(0);
    try {
        size_t pos = 0;
        int value = std::stoi(duration_str, &pos);
        std::string unit = duration_str.substr(pos);
        if (unit == "H" || unit == "h") return std::chrono::hours(value);
        if (unit == "m") return std::chrono::minutes(value);
        if (unit == "s") return std::chrono::seconds(value);
        if (unit == "ms") return std::chrono::milliseconds(value);
    } catch (...) {}
    return std::chrono::milliseconds(0);
}

static std::chrono::seconds parse_duration(const std::string& duration_str) {
    return std::chrono::duration_cast<std::chrono::seconds>(parse_duration_ms(duration_str));
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

    std::string options_str = options.dump();
    const char* query = "INSERT INTO batches (id, status, options) VALUES (?, 'pending', ?)";

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) {
            CROW_LOG_ERROR << "mysql_stmt_init() failed";
            return "";
        }

        if (mysql_stmt_prepare(stmt, query, strlen(query))) {
            CROW_LOG_ERROR << "mysql_stmt_prepare() failed: " << mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            return "";
        }

        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));

        bind[0].buffer_type = MYSQL_TYPE_STRING;
        bind[0].buffer = (char*)id.c_str();
        bind[0].buffer_length = id.length();

        bind[1].buffer_type = MYSQL_TYPE_STRING;
        bind[1].buffer = (char*)options_str.c_str();
        bind[1].buffer_length = options_str.length();

        if (mysql_stmt_bind_param(stmt, bind) || mysql_stmt_execute(stmt)) {
            CROW_LOG_ERROR << "Failed to execute create_batch stmt: " << mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            return "";
        }
        mysql_stmt_close(stmt);
    }
    
    CROW_LOG_INFO << "Created new batch: " << id << " (delete_after: " << req.delete_after << ")";
    return id;
}

bool BatchManager::add_task(const std::string& batch_id, const models::AddTaskRequest& req) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    std::string status;
    std::string options_str;

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* check_query = "SELECT status, options FROM batches WHERE id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) return false;

        if (mysql_stmt_prepare(stmt, check_query, strlen(check_query))) {
            mysql_stmt_close(stmt);
            return false;
        }

        MYSQL_BIND bind[1];
        memset(bind, 0, sizeof(bind));
        bind[0].buffer_type = MYSQL_TYPE_STRING;
        bind[0].buffer = (char*)batch_id.c_str();
        bind[0].buffer_length = batch_id.length();

        mysql_stmt_bind_param(stmt, bind);
        mysql_stmt_execute(stmt);
        mysql_stmt_store_result(stmt);

        if (mysql_stmt_num_rows(stmt) == 0) {
            mysql_stmt_close(stmt);
            CROW_LOG_WARNING << "Attempted to add task to non-existent batch: " << batch_id;
            return false;
        }

        MYSQL_BIND res_bind[2];
        memset(res_bind, 0, sizeof(res_bind));
        
        char status_buf[21]; unsigned long status_len;
        std::vector<char> opt_buf(65536); unsigned long opt_len;
        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = status_buf; res_bind[0].buffer_length = sizeof(status_buf); res_bind[0].length = &status_len;
        res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = opt_buf.data(); res_bind[1].buffer_length = opt_buf.size(); res_bind[1].length = &opt_len;

        mysql_stmt_bind_result(stmt, res_bind);
        mysql_stmt_fetch(stmt);

        status = std::string(status_buf, status_len);
        options_str = std::string(opt_buf.data(), opt_len);

        mysql_stmt_close(stmt);
    }
    
    if (status != "pending" && status != "awaiting") {
        CROW_LOG_WARNING << "Attempted to add task to batch that is " << status << ": " << batch_id;
        return false;
    }

    json opts;
    try { opts = json::parse(options_str); } catch (...) { opts = json::object(); }

    int max_size = opts.value("max_batch_size", 50);
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* count_query = "SELECT COUNT(*) FROM tasks WHERE batch_id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (stmt) {
            if (!mysql_stmt_prepare(stmt, count_query, strlen(count_query))) {
                MYSQL_BIND bind[1];
                memset(bind, 0, sizeof(bind));
                bind[0].buffer_type = MYSQL_TYPE_STRING;
                bind[0].buffer = (char*)batch_id.c_str();
                bind[0].buffer_length = batch_id.length();
                mysql_stmt_bind_param(stmt, bind);
                mysql_stmt_execute(stmt);
                
                long long count = 0;
                MYSQL_BIND res_bind[1];
                memset(res_bind, 0, sizeof(res_bind));
                res_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
                res_bind[0].buffer = &count;
                mysql_stmt_bind_result(stmt, res_bind);
                if (!mysql_stmt_fetch(stmt)) {
                    if (count >= max_size) {
                        CROW_LOG_WARNING << "Batch " << batch_id << " has reached max size of " << max_size;
                        mysql_stmt_close(stmt);
                        return false;
                    }
                }
            }
            mysql_stmt_close(stmt);
        }
    }

    std::vector<std::string> allowed = opts.value("allowed_services", std::vector<std::string>{});
    if (!allowed.empty()) {
        std::string url_type = "DIRECT";
        if (req.file_id.find("dropbox.com") != std::string::npos) url_type = "DROPBOX";
        else if (req.file_id.find("drive.google.com") != std::string::npos) url_type = "GOOGLE_DRIVE";

        bool service_allowed = false;
        for (const auto& svc : allowed) { if (svc == url_type) { service_allowed = true; break; } }
        if (!service_allowed) {
            CROW_LOG_WARNING << "Service type '" << url_type << "' not allowed for URL: " << req.file_id;
            return false;
        }
    }

    std::string task_id = utils::generate_uuid_v4();
    std::string url_path = req.file_id;
    size_t query_pos = url_path.find('?');
    if (query_pos != std::string::npos) url_path = url_path.substr(0, query_pos);
    std::string ext = std::filesystem::path(url_path).extension().string();

    std::filesystem::path base_dir = ".";
    const char* storage_dir = std::getenv("STORAGE_DIRECTORY");
    if (storage_dir != nullptr) base_dir = storage_dir;
    std::filesystem::path dest_path = base_dir / batch_id / (task_id + ext);
    std::string dest_path_str = dest_path.string();

    const char* insert_query = "INSERT INTO tasks (id, batch_id, file_id, destination_path, status) VALUES (?, ?, ?, ?, 'pending')";

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) return false;
        if (!mysql_stmt_prepare(stmt, insert_query, strlen(insert_query))) {
            MYSQL_BIND bind[4];
            memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)task_id.c_str(); bind[0].buffer_length = task_id.length();
            bind[1].buffer_type = MYSQL_TYPE_STRING; bind[1].buffer = (char*)batch_id.c_str(); bind[1].buffer_length = batch_id.length();
            bind[2].buffer_type = MYSQL_TYPE_STRING; bind[2].buffer = (char*)req.file_id.c_str(); bind[2].buffer_length = req.file_id.length();
            bind[3].buffer_type = MYSQL_TYPE_STRING; bind[3].buffer = (char*)dest_path_str.c_str(); bind[3].buffer_length = dest_path_str.length();
            mysql_stmt_bind_param(stmt, bind);
            mysql_stmt_execute(stmt);
        }
        mysql_stmt_close(stmt);
    }
    
    CROW_LOG_INFO << "Added task " << task_id << " to batch " << batch_id;
    return true;
}

bool BatchManager::start_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* check_query = "SELECT status FROM batches WHERE id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (stmt) {
            if (!mysql_stmt_prepare(stmt, check_query, strlen(check_query))) {
                MYSQL_BIND bind[1];
                memset(bind, 0, sizeof(bind));
                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)batch_id.c_str(); bind[0].buffer_length = batch_id.length();
                mysql_stmt_bind_param(stmt, bind);
                mysql_stmt_execute(stmt);
                mysql_stmt_store_result(stmt);

                char status_buf[21]; unsigned long status_len;
                MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = status_buf; res_bind[0].buffer_length = sizeof(status_buf); res_bind[0].length = &status_len;
                mysql_stmt_bind_result(stmt, res_bind);
                if (!mysql_stmt_fetch(stmt)) {
                    std::string status(status_buf, status_len);
                    if (status == "pending") {
                        const char* update_query = "UPDATE batches SET status = 'awaiting' WHERE id = ?";
                        MYSQL_STMT* u_stmt = mysql_stmt_init(conn);
                        if (u_stmt) {
                            if (!mysql_stmt_prepare(u_stmt, update_query, strlen(update_query))) {
                                mysql_stmt_bind_param(u_stmt, bind);
                                mysql_stmt_execute(u_stmt);
                            }
                            mysql_stmt_close(u_stmt);
                        }
                    }
                }
            }
            mysql_stmt_close(stmt);
        }
    }

    CROW_LOG_INFO << "Started batch (moved to awaiting): " << batch_id;
    
    static std::once_flag monitor_started;
    std::call_once(monitor_started, [this]() {
        // Task Processing Monitor
        std::thread([this]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                MYSQL* conn = DatabaseService::get_instance().get_connection();
                struct BatchInfo { std::string id; long long wait_ms; };
                std::vector<BatchInfo> awaiting_batches;

                {
                    std::lock_guard<std::mutex> lock(db_mutex_);
                    const char* poll_query = "SELECT id, options FROM batches WHERE status = 'awaiting'";
                    MYSQL_STMT* stmt = mysql_stmt_init(conn);
                    if (stmt) {
                        if (!mysql_stmt_prepare(stmt, poll_query, strlen(poll_query))) {
                            mysql_stmt_execute(stmt);
                            mysql_stmt_store_result(stmt);

                            char id_buf[37]; unsigned long id_len;
                            std::vector<char> opt_buf(65536); unsigned long opt_len;
                            MYSQL_BIND res_bind[2]; memset(res_bind, 0, sizeof(res_bind));
                            res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
                            res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = opt_buf.data(); res_bind[1].buffer_length = opt_buf.size(); res_bind[1].length = &opt_len;
                            mysql_stmt_bind_result(stmt, res_bind);
                            while (!mysql_stmt_fetch(stmt)) {
                                try {
                                    json opts = json::parse(std::string(opt_buf.data(), opt_len));
                                    awaiting_batches.push_back({std::string(id_buf, id_len), parse_duration_ms(opts.value("wait_duration", "5s")).count()});
                                } catch (...) {}
                            }
                        }
                        mysql_stmt_close(stmt);
                    }
                }

                for (const auto& b : awaiting_batches) {
                    struct TaskInfo { std::string id, file_id, dest; };
                    std::vector<TaskInfo> tasks_to_do;
                    {
                        std::lock_guard<std::mutex> lock(db_mutex_);
                        const char* task_query = "SELECT id, file_id, destination_path, created_at FROM tasks WHERE batch_id = ? AND status = 'pending' ORDER BY created_at ASC";
                        MYSQL_STMT* stmt = mysql_stmt_init(conn);
                        if (stmt) {
                            if (!mysql_stmt_prepare(stmt, task_query, strlen(task_query))) {
                                MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)b.id.c_str(); bind[0].buffer_length = b.id.length();
                                mysql_stmt_bind_param(stmt, bind);
                                mysql_stmt_execute(stmt);
                                mysql_stmt_store_result(stmt);

                                if (mysql_stmt_num_rows(stmt) > 0) {
                                    MYSQL_BIND res_bind[4]; memset(res_bind, 0, sizeof(res_bind));
                                    char id_buf[37]; unsigned long id_len;
                                    std::vector<char> f_id_buf(2048); unsigned long f_id_len;
                                    std::vector<char> dest_buf(2048); unsigned long dest_len;
                                    char created_buf[30]; unsigned long created_len;
                                    res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
                                    res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = f_id_buf.data(); res_bind[1].buffer_length = f_id_buf.size(); res_bind[1].length = &f_id_len;
                                    res_bind[2].buffer_type = MYSQL_TYPE_STRING; res_bind[2].buffer = dest_buf.data(); res_bind[2].buffer_length = dest_buf.size(); res_bind[2].length = &dest_len;
                                    res_bind[3].buffer_type = MYSQL_TYPE_STRING; res_bind[3].buffer = created_buf; res_bind[3].buffer_length = sizeof(created_buf); res_bind[3].length = &created_len;
                                    mysql_stmt_bind_result(stmt, res_bind);
                                    mysql_stmt_fetch(stmt);

                                    std::string oldest_created(created_buf, created_len);
                                    double elapsed_ms = 0;
                                    const char* time_query = "SELECT (UNIX_TIMESTAMP(NOW()) - UNIX_TIMESTAMP(?)) * 1000";
                                    MYSQL_STMT* t_stmt = mysql_stmt_init(conn);
                                    if (t_stmt) {
                                        if (!mysql_stmt_prepare(t_stmt, time_query, strlen(time_query))) {
                                            MYSQL_BIND t_bind[1]; memset(t_bind, 0, sizeof(t_bind));
                                            t_bind[0].buffer_type = MYSQL_TYPE_STRING; t_bind[0].buffer = (char*)oldest_created.c_str(); t_bind[0].buffer_length = oldest_created.length();
                                            mysql_stmt_bind_param(t_stmt, t_bind); mysql_stmt_execute(t_stmt);
                                            MYSQL_BIND tr_bind[1]; memset(tr_bind, 0, sizeof(tr_bind));
                                            tr_bind[0].buffer_type = MYSQL_TYPE_DOUBLE; tr_bind[0].buffer = &elapsed_ms;
                                            mysql_stmt_bind_result(t_stmt, tr_bind); mysql_stmt_fetch(t_stmt);
                                        }
                                        mysql_stmt_close(t_stmt);
                                    }

                                    if (elapsed_ms >= b.wait_ms) {
                                        tasks_to_do.push_back({std::string(id_buf, id_len), std::string(f_id_buf.data(), f_id_len), std::string(dest_buf.data(), dest_len)});
                                        std::vector<std::string> ids_to_mark = {tasks_to_do.back().id};
                                        while (!mysql_stmt_fetch(stmt)) {
                                            tasks_to_do.push_back({std::string(id_buf, id_len), std::string(f_id_buf.data(), f_id_len), std::string(dest_buf.data(), dest_len)});
                                            ids_to_mark.push_back(tasks_to_do.back().id);
                                        }

                                        std::string mark_query = "UPDATE tasks SET status = 'processing' WHERE id IN (";
                                        for (size_t i = 0; i < ids_to_mark.size(); ++i) mark_query += (i == 0 ? "?" : ",?");
                                        mark_query += ")";
                                        MYSQL_STMT* m_stmt = mysql_stmt_init(conn);
                                        if (m_stmt) {
                                            if (!mysql_stmt_prepare(m_stmt, mark_query.c_str(), mark_query.length())) {
                                                std::vector<MYSQL_BIND> m_binds(ids_to_mark.size()); memset(m_binds.data(), 0, m_binds.size() * sizeof(MYSQL_BIND));
                                                for (size_t i = 0; i < ids_to_mark.size(); ++i) {
                                                    m_binds[i].buffer_type = MYSQL_TYPE_STRING; m_binds[i].buffer = (char*)ids_to_mark[i].c_str(); m_binds[i].buffer_length = ids_to_mark[i].length();
                                                }
                                                mysql_stmt_bind_param(m_stmt, m_binds.data()); mysql_stmt_execute(m_stmt);
                                            }
                                            mysql_stmt_close(m_stmt);
                                        }
                                    }
                                }
                            }
                            mysql_stmt_close(stmt);
                        }
                    }

                    if (!tasks_to_do.empty()) {
                        std::thread([this, tasks_to_do, b_id = b.id]() {
                            MYSQL* t_conn = DatabaseService::get_instance().get_connection();
                            int max_retries = 3; long long max_bytes = 0; long long inter_task_delay_ms = 5000;
                            {
                                std::lock_guard<std::mutex> lock(db_mutex_);
                                const char* opt_query = "SELECT options FROM batches WHERE id = ?";
                                MYSQL_STMT* stmt = mysql_stmt_init(t_conn);
                                if (stmt) {
                                    if (!mysql_stmt_prepare(stmt, opt_query, strlen(opt_query))) {
                                        MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                        bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)b_id.c_str(); bind[0].buffer_length = b_id.length();
                                        mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt);
                                        std::vector<char> opt_buf(65536); unsigned long opt_len;
                                        MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                                        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = opt_buf.data(); res_bind[0].buffer_length = opt_buf.size(); res_bind[0].length = &opt_len;
                                        mysql_stmt_bind_result(stmt, res_bind);
                                        if (!mysql_stmt_fetch(stmt)) {
                                            try {
                                                json opts = json::parse(std::string(opt_buf.data(), opt_len));
                                                max_retries = opts.value("max_retries", 3);
                                                max_bytes = utils::parse_size_string(opts.value("max_batch_storage", "0"));
                                                inter_task_delay_ms = parse_duration_ms(opts.value("wait_duration", "5s")).count();
                                            } catch (...) {}
                                        }
                                    }
                                    mysql_stmt_close(stmt);
                                }
                            }

                            for (size_t i = 0; i < tasks_to_do.size(); ++i) {
                                const auto& t = tasks_to_do[i];
                                {
                                    std::lock_guard<std::mutex> lock(db_mutex_);
                                    const char* update_status_query = "UPDATE tasks SET status = 'downloading' WHERE id = ?";
                                    MYSQL_STMT* stmt = mysql_stmt_init(t_conn);
                                    if (stmt) {
                                        if (!mysql_stmt_prepare(stmt, update_status_query, strlen(update_status_query))) {
                                            MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                            bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)t.id.c_str(); bind[0].buffer_length = t.id.length();
                                            mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt);
                                        }
                                        mysql_stmt_close(stmt);
                                    }
                                }
                                
                                bool success = false; int attempts = 0;
                                while (attempts <= max_retries) {
                                    long long allowed_bytes = max_bytes;
                                    if (max_bytes > 0) {
                                        long long current_batch_size = 0;
                                        {
                                            std::lock_guard<std::mutex> lock(db_mutex_);
                                            const char* size_query = "SELECT SUM(m.file_size) FROM media_metadata m JOIN tasks t ON m.task_id = t.id WHERE t.batch_id = ?";
                                            MYSQL_STMT* stmt = mysql_stmt_init(t_conn);
                                            if (stmt) {
                                                if (!mysql_stmt_prepare(stmt, size_query, strlen(size_query))) {
                                                    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                                    bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)b_id.c_str(); bind[0].buffer_length = b_id.length();
                                                    mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt);
                                                    MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                                                    res_bind[0].buffer_type = MYSQL_TYPE_LONGLONG; res_bind[0].buffer = &current_batch_size;
                                                    mysql_stmt_bind_result(stmt, res_bind); mysql_stmt_fetch(stmt);
                                                }
                                                mysql_stmt_close(stmt);
                                            }
                                        }
                                        allowed_bytes = max_bytes - current_batch_size;
                                        if (allowed_bytes <= 0) break;
                                    }

                                    DownloadResult dl_res = DownloadService::download_file(t.file_id, t.dest, allowed_bytes);
                                    if (dl_res.success) { success = true; break; }
                                    attempts++;
                                    if (attempts <= max_retries) {
                                        int wait_time = dl_res.rate_limited ? dl_res.retry_after_seconds : 2;
                                        std::this_thread::sleep_for(std::chrono::seconds(wait_time));
                                    }
                                }

                                std::string final_status = success ? "success" : "failed";
                                std::string local_url_str = success ? "file://" + t.dest : "";
                                {
                                    std::lock_guard<std::mutex> lock(db_mutex_);
                                    const char* update_query = "UPDATE tasks SET status = ?, local_url = ? WHERE id = ?";
                                    MYSQL_STMT* stmt = mysql_stmt_init(t_conn);
                                    if (stmt) {
                                        if (!mysql_stmt_prepare(stmt, update_query, strlen(update_query))) {
                                            MYSQL_BIND bind[3]; memset(bind, 0, sizeof(bind));
                                            bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)final_status.c_str(); bind[0].buffer_length = final_status.length();
                                            bind[1].buffer_type = MYSQL_TYPE_STRING; bind[1].buffer = (char*)local_url_str.c_str(); bind[1].buffer_length = local_url_str.length();
                                            bind[2].buffer_type = MYSQL_TYPE_STRING; bind[2].buffer = (char*)t.id.c_str(); bind[2].buffer_length = t.id.length();
                                            mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt);
                                        }
                                        mysql_stmt_close(stmt);
                                    }
                                }

                                if (success) {
                                    int res_val = 512;
                                    {
                                        std::lock_guard<std::mutex> lock(db_mutex_);
                                        const char* res_query = "SELECT options FROM batches WHERE id = ?";
                                        MYSQL_STMT* stmt = mysql_stmt_init(t_conn);
                                        if (stmt) {
                                            if (!mysql_stmt_prepare(stmt, res_query, strlen(res_query))) {
                                                MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)b_id.c_str(); bind[0].buffer_length = b_id.length();
                                                mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt);
                                                std::vector<char> opt_buf(65536); unsigned long opt_len;
                                                MYSQL_BIND r_bind[1]; memset(r_bind, 0, sizeof(r_bind));
                                                r_bind[0].buffer_type = MYSQL_TYPE_STRING; r_bind[0].buffer = opt_buf.data(); r_bind[0].buffer_length = opt_buf.size(); r_bind[0].length = &opt_len;
                                                mysql_stmt_bind_result(stmt, r_bind);
                                                if (!mysql_stmt_fetch(stmt)) {
                                                    try { json opts = json::parse(std::string(opt_buf.data(), opt_len)); res_val = opts.value("waveform_resolution", 512); } catch (...) {}
                                                }
                                            }
                                            mysql_stmt_close(stmt);
                                        }
                                    }

                                    MediaResult m_res = MediaService::extract_waveform(t.dest, res_val);
                                    if (m_res.success) {
                                        std::lock_guard<std::mutex> lock(db_mutex_);
                                        const char* meta_query = "INSERT INTO media_metadata (task_id, file_size, format, duration_seconds) VALUES (?, ?, ?, ?) ON DUPLICATE KEY UPDATE file_size=VALUES(file_size), format=VALUES(format), duration_seconds=VALUES(duration_seconds)";
                                        MYSQL_STMT* m_stmt = mysql_stmt_init(t_conn);
                                        if (m_stmt) {
                                            if (!mysql_stmt_prepare(m_stmt, meta_query, strlen(meta_query))) {
                                                MYSQL_BIND bind[4]; memset(bind, 0, sizeof(bind));
                                                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)t.id.c_str(); bind[0].buffer_length = t.id.length();
                                                bind[1].buffer_type = MYSQL_TYPE_LONGLONG; bind[1].buffer = &m_res.file_size;
                                                bind[2].buffer_type = MYSQL_TYPE_STRING; bind[2].buffer = (char*)m_res.format.c_str(); bind[2].buffer_length = m_res.format.length();
                                                bind[3].buffer_type = MYSQL_TYPE_FLOAT; bind[3].buffer = &m_res.duration_seconds;
                                                mysql_stmt_bind_param(m_stmt, bind); mysql_stmt_execute(m_stmt);
                                            }
                                            mysql_stmt_close(m_stmt);
                                        }

                                        std::string wave_str = json(m_res.waveform_data).dump();
                                        const char* wave_query = "INSERT INTO waveforms (task_id, waveform_data, resolution) VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE waveform_data=VALUES(waveform_data), resolution=VALUES(resolution)";
                                        MYSQL_STMT* w_stmt = mysql_stmt_init(t_conn);
                                        if (w_stmt) {
                                            if (!mysql_stmt_prepare(w_stmt, wave_query, strlen(wave_query))) {
                                                MYSQL_BIND bind[3]; memset(bind, 0, sizeof(bind));
                                                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)t.id.c_str(); bind[0].buffer_length = t.id.length();
                                                bind[1].buffer_type = MYSQL_TYPE_BLOB; bind[1].buffer = (char*)wave_str.c_str(); bind[1].buffer_length = wave_str.length();
                                                bind[2].buffer_type = MYSQL_TYPE_LONG; bind[2].buffer = &res_val;
                                                mysql_stmt_bind_param(w_stmt, bind); mysql_stmt_execute(w_stmt);
                                            }
                                            mysql_stmt_close(w_stmt);
                                        }
                                    }
                                }
                                if (i < tasks_to_do.size() - 1) std::this_thread::sleep_for(std::chrono::milliseconds(inter_task_delay_ms));
                            }
                        }).detach();
                    }
                }
            }
        }).detach();

        // Cleanup Monitor
        std::thread([this]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(60));
                MYSQL* conn = DatabaseService::get_instance().get_connection();
                std::vector<std::string> to_delete;
                {
                    std::lock_guard<std::mutex> lock(db_mutex_);
                    const char* cleanup_query = "SELECT id FROM batches WHERE status = 'completed' AND delete_at IS NOT NULL AND delete_at <= NOW()";
                    MYSQL_STMT* stmt = mysql_stmt_init(conn);
                    if (stmt) {
                        if (!mysql_stmt_prepare(stmt, cleanup_query, strlen(cleanup_query))) {
                            mysql_stmt_execute(stmt); mysql_stmt_store_result(stmt);
                            char id_buf[37]; unsigned long id_len;
                            MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                            res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
                            mysql_stmt_bind_result(stmt, res_bind);
                            while (!mysql_stmt_fetch(stmt)) to_delete.push_back(std::string(id_buf, id_len));
                        }
                        mysql_stmt_close(stmt);
                    }
                }

                for (const auto& b_id : to_delete) {
                    std::filesystem::path base_dir = ".";
                    const char* storage_dir = std::getenv("STORAGE_DIRECTORY");
                    if (storage_dir != nullptr) base_dir = storage_dir;
                    try {
                        if (std::filesystem::exists(base_dir / b_id)) std::filesystem::remove_all(base_dir / b_id);
                        std::lock_guard<std::mutex> lock(db_mutex_);
                        const char* update_query = "UPDATE batches SET status = 'deleted', delete_at = NULL WHERE id = ?";
                        MYSQL_STMT* stmt = mysql_stmt_init(conn);
                        if (stmt) {
                            if (!mysql_stmt_prepare(stmt, update_query, strlen(update_query))) {
                                MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)b_id.c_str(); bind[0].buffer_length = b_id.length();
                                mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt);
                            }
                            mysql_stmt_close(stmt);
                        }
                    } catch (...) {}
                }
            }
        }).detach();
    });
    
    return true;
}

bool BatchManager::complete_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    long long delete_after_seconds = 0;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* check_query = "SELECT options FROM batches WHERE id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (stmt) {
            if (!mysql_stmt_prepare(stmt, check_query, strlen(check_query))) {
                MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)batch_id.c_str(); bind[0].buffer_length = batch_id.length();
                mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt);
                std::vector<char> opt_buf(65536); unsigned long opt_len;
                MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = opt_buf.data(); res_bind[0].buffer_length = opt_buf.size(); res_bind[0].length = &opt_len;
                mysql_stmt_bind_result(stmt, res_bind);
                if (!mysql_stmt_fetch(stmt)) {
                    try { json opts = json::parse(std::string(opt_buf.data(), opt_len)); delete_after_seconds = parse_duration(opts.value("delete_after", "0s")).count(); } catch (...) {}
                }
            }
            mysql_stmt_close(stmt);
        }
    }

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* query = (delete_after_seconds > 0) ? "UPDATE batches SET status = 'completed', delete_at = DATE_ADD(NOW(), INTERVAL ? SECOND) WHERE id = ? AND status = 'awaiting'" : "UPDATE batches SET status = 'completed' WHERE id = ? AND status = 'awaiting'";
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) return false;
        if (!mysql_stmt_prepare(stmt, query, strlen(query))) {
            MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
            if (delete_after_seconds > 0) {
                bind[0].buffer_type = MYSQL_TYPE_LONGLONG; bind[0].buffer = &delete_after_seconds;
                bind[1].buffer_type = MYSQL_TYPE_STRING; bind[1].buffer = (char*)batch_id.c_str(); bind[1].buffer_length = batch_id.length();
            } else {
                bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)batch_id.c_str(); bind[0].buffer_length = batch_id.length();
            }
            mysql_stmt_bind_param(stmt, bind);
            mysql_stmt_execute(stmt);
            if (mysql_stmt_affected_rows(stmt) == 0) { mysql_stmt_close(stmt); return false; }
        }
        mysql_stmt_close(stmt);
    }
    return true;
}

std::optional<models::Batch> BatchManager::get_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    models::Batch b; b.id = batch_id;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* query = "SELECT status, options FROM batches WHERE id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (!stmt) return std::nullopt;
        if (!mysql_stmt_prepare(stmt, query, strlen(query))) {
            MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)batch_id.c_str(); bind[0].buffer_length = batch_id.length();
            mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt); mysql_stmt_store_result(stmt);
            if (mysql_stmt_num_rows(stmt) == 0) { mysql_stmt_close(stmt); return std::nullopt; }
            char status_buf[21]; unsigned long status_len; std::vector<char> opt_buf(65536); unsigned long opt_len;
            MYSQL_BIND res_bind[2]; memset(res_bind, 0, sizeof(res_bind));
            res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = status_buf; res_bind[0].buffer_length = sizeof(status_buf); res_bind[0].length = &status_len;
            res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = opt_buf.data(); res_bind[1].buffer_length = opt_buf.size(); res_bind[1].length = &opt_len;
            mysql_stmt_bind_result(stmt, res_bind); mysql_stmt_fetch(stmt);
            b.status = std::string(status_buf, status_len);
            try {
                json opts = json::parse(std::string(opt_buf.data(), opt_len));
                b.options.wait_duration = opts.value("wait_duration", "5s"); b.options.max_retries = opts.value("max_retries", 0);
                b.options.max_batch_size = opts.value("max_batch_size", 0); b.options.max_batch_storage = opts.value("max_batch_storage", "");
                b.options.allowed_services = opts.value("allowed_services", std::vector<std::string>{}); b.options.delete_after = opts.value("delete_after", ""); b.options.waveform_resolution = opts.value("waveform_resolution", 512);
            } catch (...) {}
            mysql_stmt_close(stmt);

            const char* task_query = "SELECT t.id, t.file_id, t.destination_path, t.status, t.local_url, m.file_size, m.format, m.duration_seconds, w.waveform_data, w.resolution FROM tasks t LEFT JOIN media_metadata m ON t.id = m.task_id LEFT JOIN waveforms w ON t.id = w.task_id WHERE t.batch_id = ?";
            stmt = mysql_stmt_init(conn);
            if (stmt && !mysql_stmt_prepare(stmt, task_query, strlen(task_query))) {
                mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt); mysql_stmt_store_result(stmt);
                MYSQL_BIND res_bind_tasks[10]; memset(res_bind_tasks, 0, sizeof(res_bind_tasks));
                char id_buf[37]; unsigned long id_len; std::vector<char> f_id_buf(2048); unsigned long f_id_len; std::vector<char> dest_buf(2048); unsigned long dest_len; char t_s_buf[21]; unsigned long t_s_len; std::vector<char> url_buf(2048); unsigned long url_len; long long f_size = 0; char f_size_null; char format_buf[11]; unsigned long format_len; char format_null; float dur = 0; char dur_null; std::vector<char> w_buf(1024*1024); unsigned long w_len; char w_null; int res_val = 0; char res_null;
                res_bind_tasks[0].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[0].buffer = id_buf; res_bind_tasks[0].buffer_length = sizeof(id_buf); res_bind_tasks[0].length = &id_len;
                res_bind_tasks[1].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[1].buffer = f_id_buf.data(); res_bind_tasks[1].buffer_length = f_id_buf.size(); res_bind_tasks[1].length = &f_id_len;
                res_bind_tasks[2].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[2].buffer = dest_buf.data(); res_bind_tasks[2].buffer_length = dest_buf.size(); res_bind_tasks[2].length = &dest_len;
                res_bind_tasks[3].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[3].buffer = t_s_buf; res_bind_tasks[3].buffer_length = sizeof(t_s_buf); res_bind_tasks[3].length = &t_s_len;
                res_bind_tasks[4].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[4].buffer = url_buf.data(); res_bind_tasks[4].buffer_length = url_buf.size(); res_bind_tasks[4].length = &url_len;
                res_bind_tasks[5].buffer_type = MYSQL_TYPE_LONGLONG; res_bind_tasks[5].buffer = &f_size; res_bind_tasks[5].is_null = (char*)&f_size_null;
                res_bind_tasks[6].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[6].buffer = format_buf; res_bind_tasks[6].buffer_length = sizeof(format_buf); res_bind_tasks[6].length = &format_len; res_bind_tasks[6].is_null = (char*)&format_null;
                res_bind_tasks[7].buffer_type = MYSQL_TYPE_FLOAT; res_bind_tasks[7].buffer = &dur; res_bind_tasks[7].is_null = (char*)&dur_null;
                res_bind_tasks[8].buffer_type = MYSQL_TYPE_BLOB; res_bind_tasks[8].buffer = w_buf.data(); res_bind_tasks[8].buffer_length = w_buf.size(); res_bind_tasks[8].length = &w_len; res_bind_tasks[8].is_null = (char*)&w_null;
                res_bind_tasks[9].buffer_type = MYSQL_TYPE_LONG; res_bind_tasks[9].buffer = &res_val; res_bind_tasks[9].is_null = (char*)&res_null;
                mysql_stmt_bind_result(stmt, res_bind_tasks);
                while (!mysql_stmt_fetch(stmt)) {
                    models::Task t; t.id = std::string(id_buf, id_len); t.file_id = std::string(f_id_buf.data(), f_id_len); t.destination_path = std::string(dest_buf.data(), dest_len); t.status = std::string(t_s_buf, t_s_len); t.local_url = std::string(url_buf.data(), url_len);
                    if (!f_size_null) { models::TaskMetadata m; m.file_size = f_size; m.format = format_null ? "" : std::string(format_buf, format_len); m.duration_seconds = dur_null ? 0 : dur; t.metadata = m; }
                    if (!w_null) { try { t.waveform = json::parse(std::string(w_buf.data(), w_len)).get<std::vector<float>>(); t.waveform_resolution = res_null ? 0 : res_val; } catch (...) {} }
                    b.tasks.push_back(t);
                }
                mysql_stmt_close(stmt);
            }
        }
    }
    return b;
}

std::optional<models::Task> BatchManager::get_ingested_task(const std::string& task_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    models::Task t;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* query = "SELECT t.id, t.file_id, t.destination_path, t.status, t.local_url, m.file_size, m.format, m.duration_seconds, w.waveform_data, w.resolution FROM tasks t LEFT JOIN media_metadata m ON t.id = m.task_id LEFT JOIN waveforms w ON t.id = w.task_id WHERE t.id = ?";
        MYSQL_STMT* stmt = mysql_stmt_init(conn);
        if (stmt && !mysql_stmt_prepare(stmt, query, strlen(query))) {
            MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
            bind[0].buffer_type = MYSQL_TYPE_STRING; bind[0].buffer = (char*)task_id.c_str(); bind[0].buffer_length = task_id.length();
            mysql_stmt_bind_param(stmt, bind); mysql_stmt_execute(stmt); mysql_stmt_store_result(stmt);
            if (mysql_stmt_num_rows(stmt) == 0) { mysql_stmt_close(stmt); return std::nullopt; }
            MYSQL_BIND res_bind[10]; memset(res_bind, 0, sizeof(res_bind));
            char id_buf[37]; unsigned long id_len; std::vector<char> f_id_buf(2048); unsigned long f_id_len; std::vector<char> dest_buf(2048); unsigned long dest_len; char s_buf[21]; unsigned long s_len; std::vector<char> url_buf(2048); unsigned long url_len; long long f_size = 0; char f_size_null; char format_buf[11]; unsigned long format_len; char format_null; float dur = 0; char dur_null; std::vector<char> w_buf(1024*1024); unsigned long w_len; char w_null; int res_val = 0; char res_null;
            res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
            res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = f_id_buf.data(); res_bind[1].buffer_length = f_id_buf.size(); res_bind[1].length = &f_id_len;
            res_bind[2].buffer_type = MYSQL_TYPE_STRING; res_bind[2].buffer = dest_buf.data(); res_bind[2].buffer_length = dest_buf.size(); res_bind[2].length = &dest_len;
            res_bind[3].buffer_type = MYSQL_TYPE_STRING; res_bind[3].buffer = s_buf; res_bind[3].buffer_length = sizeof(s_buf); res_bind[3].length = &s_len;
            res_bind[4].buffer_type = MYSQL_TYPE_STRING; res_bind[4].buffer = url_buf.data(); res_bind[4].buffer_length = url_buf.size(); res_bind[4].length = &url_len;
            res_bind[5].buffer_type = MYSQL_TYPE_LONGLONG; res_bind[5].buffer = &f_size; res_bind[5].is_null = (char*)&f_size_null;
            res_bind[6].buffer_type = MYSQL_TYPE_STRING; res_bind[6].buffer = format_buf; res_bind[6].buffer_length = sizeof(format_buf); res_bind[6].length = &format_len; res_bind[6].is_null = (char*)&format_null;
            res_bind[7].buffer_type = MYSQL_TYPE_FLOAT; res_bind[7].buffer = &dur; res_bind[7].is_null = (char*)&dur_null;
            res_bind[8].buffer_type = MYSQL_TYPE_BLOB; res_bind[8].buffer = w_buf.data(); res_bind[8].buffer_length = w_buf.size(); res_bind[8].length = &w_len; res_bind[8].is_null = (char*)&w_null;
            res_bind[9].buffer_type = MYSQL_TYPE_LONG; res_bind[9].buffer = &res_val; res_bind[9].is_null = (char*)&res_null;
            mysql_stmt_bind_result(stmt, res_bind); mysql_stmt_fetch(stmt);
            t.id = std::string(id_buf, id_len); t.file_id = std::string(f_id_buf.data(), f_id_len); t.destination_path = std::string(dest_buf.data(), dest_len); t.status = std::string(s_buf, s_len); t.local_url = std::string(url_buf.data(), url_len);
            if (!f_size_null) { models::TaskMetadata m; m.file_size = f_size; m.format = format_null ? "" : std::string(format_buf, format_len); m.duration_seconds = dur_null ? 0 : dur; t.metadata = m; }
            if (!w_null) { try { t.waveform = json::parse(std::string(w_buf.data(), w_len)).get<std::vector<float>>(); t.waveform_resolution = res_null ? 0 : res_val; } catch (...) {} }
            mysql_stmt_close(stmt);
        }
    }
    return t;
}

} // namespace services
