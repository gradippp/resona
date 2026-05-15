#include "batch_manager.h"
#include "download_service.h"
#include "media_service.h"
#include "database_service.h"
#include "../utils/uuid.h"
#include "../utils/size_parser.h"
#include "../utils/base64.h"
#include "../utils/magic_bytes.h"
#include "../utils/db_utils.h"
#include "../utils/time_utils.h"
#include "crow/logging.h"
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace services {

BatchManager::BatchManager() : stop_flag_(false) {
    const char* env_dir = std::getenv("STORAGE_DIRECTORY");
    storage_dir_ = env_dir ? env_dir : "data";
    if (!std::filesystem::exists(storage_dir_)) {
        std::filesystem::create_directories(storage_dir_);
        CROW_LOG_INFO << "Created storage directory: " << storage_dir_;
    }
}

BatchManager::~BatchManager() {
    stop();
}

void BatchManager::stop() {
    if (stop_flag_.exchange(true)) return;
    
    CROW_LOG_INFO << "Stopping BatchManager monitors...";
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        task_cv_.notify_all();
    }

    std::vector<std::thread> to_join;
    {
        std::lock_guard<std::mutex> lock(thread_mutex_);
        to_join.swap(monitor_threads_);
    }

    for (auto& t : to_join) {
        if (t.joinable()) t.join();
    }
    CROW_LOG_INFO << "BatchManager stopped.";
}

json BatchManager::parse_batch_options(const std::string& options_str) {
    json opts;
    try {
        opts = json::parse(options_str);
    } catch (...) {
        opts = json::object();
    }
    
    // Ensure all expected keys have defaults if missing
    auto ensure_default = [&](const std::string& key, const json& val) {
        if (!opts.contains(key) || opts[key].is_null()) opts[key] = val;
    };

    ensure_default("wait_duration", "5s");
    ensure_default("max_retries", 3);
    ensure_default("max_batch_size", 50);
    ensure_default("max_batch_storage", "5G");
    ensure_default("allowed_services", json::array());
    ensure_default("allowed_content_types", json::array());
    ensure_default("delete_after", "24H");
    ensure_default("waveform_resolution", 4096);
    ensure_default("transcode_formats", json::array());
    
    return opts;
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
        {"allowed_content_types", req.allowed_content_types},
        {"delete_after", req.delete_after},
        {"waveform_resolution", req.waveform_resolution},
        {"transcode_formats", req.transcode_formats}
    };

    std::string options_str = options.dump();
    const char* query = "INSERT INTO batches (id, status, options) VALUES (?, 'pending', ?)";

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        utils::StatementWrapper stmt(conn);
        if (!stmt.isValid() || !stmt.prepare(query)) return "";

        MYSQL_BIND bind[2];
        memset(bind, 0, sizeof(bind));
        stmt.bind_string_param(0, id, bind);
        stmt.bind_string_param(1, options_str, bind);

        if (!stmt.bind_params(bind) || !stmt.execute()) return "";
    }
    
    CROW_LOG_INFO << "Created new batch: " << id << " (delete_after: " << req.delete_after << ")";
    return id;
}

std::optional<std::string> BatchManager::add_task(const std::string& batch_id, const models::AddTaskRequest& req) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    std::string status;
    std::string options_str;

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* check_query = "SELECT status, options FROM batches WHERE id = ?";
        utils::StatementWrapper stmt(conn);
        if (!stmt.isValid() || !stmt.prepare(check_query)) return std::nullopt;

        MYSQL_BIND bind[1];
        memset(bind, 0, sizeof(bind));
        stmt.bind_string_param(0, batch_id, bind);

        if (!stmt.bind_params(bind) || !stmt.execute() || !stmt.store_result()) return std::nullopt;

        if (stmt.num_rows() == 0) {
            CROW_LOG_WARNING << "Attempted to add task to non-existent batch: " << batch_id;
            return std::nullopt;
        }

        MYSQL_BIND res_bind[2];
        memset(res_bind, 0, sizeof(res_bind));
        
        char status_buf[21]; unsigned long status_len = 0;
        std::vector<char> opt_buf(65536); unsigned long opt_len = 0;
        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = status_buf; res_bind[0].buffer_length = sizeof(status_buf); res_bind[0].length = &status_len;
        res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = opt_buf.data(); res_bind[1].buffer_length = opt_buf.size(); res_bind[1].length = &opt_len;

        if (!stmt.bind_result(res_bind) || stmt.fetch()) return std::nullopt;

        status = std::string(status_buf, status_len);
        options_str = std::string(opt_buf.data(), opt_len);
    }
    
    if (status != "pending" && status != "awaiting") {
        CROW_LOG_WARNING << "Attempted to add task to batch that is " << status << ": " << batch_id;
        return std::nullopt;
    }

    json opts = parse_batch_options(options_str);

    int max_size = opts.value("max_batch_size", 50);
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* count_query = "SELECT COUNT(*) FROM tasks WHERE batch_id = ?";
        utils::StatementWrapper stmt(conn);
        if (stmt.isValid() && stmt.prepare(count_query)) {
            MYSQL_BIND bind[1];
            memset(bind, 0, sizeof(bind));
            stmt.bind_string_param(0, batch_id, bind);
            if (stmt.bind_params(bind) && stmt.execute()) {
                long long count = 0;
                MYSQL_BIND res_bind[1];
                memset(res_bind, 0, sizeof(res_bind));
                res_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
                res_bind[0].buffer = &count;
                if (stmt.bind_result(res_bind) && !stmt.fetch()) {
                    if (count >= max_size) {
                        CROW_LOG_WARNING << "Batch " << batch_id << " has reached max size of " << max_size;
                        return std::nullopt;
                    }
                }
            }
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
            return std::nullopt;
        }
    }

    std::string task_id = utils::generate_uuid_v4();
    std::string url_path = req.file_id;
    size_t query_pos = url_path.find('?');
    if (query_pos != std::string::npos) url_path = url_path.substr(0, query_pos);
    std::string ext = std::filesystem::path(url_path).extension().string();

    std::filesystem::path base_dir = storage_dir_;
    std::filesystem::path dest_path = base_dir / batch_id / (task_id + ext);
    std::string dest_path_str = dest_path.string();

    const char* insert_query = "INSERT INTO tasks (id, batch_id, file_id, content_type, destination_path, status) VALUES (?, ?, ?, '', ?, 'pending')";

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        utils::StatementWrapper stmt(conn);
        if (!stmt.isValid() || !stmt.prepare(insert_query)) return std::nullopt;
        
        MYSQL_BIND bind[4];
        memset(bind, 0, sizeof(bind));
        stmt.bind_string_param(0, task_id, bind);
        stmt.bind_string_param(1, batch_id, bind);
        stmt.bind_string_param(2, req.file_id, bind);
        stmt.bind_string_param(3, dest_path_str, bind);
        if (!stmt.bind_params(bind) || !stmt.execute()) return std::nullopt;
    }
    
    CROW_LOG_INFO << "Added task " << task_id << " to batch " << batch_id;
    task_cv_.notify_one();
    return task_id;
}

bool BatchManager::start_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* check_query = "SELECT status FROM batches WHERE id = ?";
        utils::StatementWrapper stmt(conn);
        if (stmt.isValid() && stmt.prepare(check_query)) {
            MYSQL_BIND bind[1];
            memset(bind, 0, sizeof(bind));
            stmt.bind_string_param(0, batch_id, bind);
            
            if (stmt.bind_params(bind) && stmt.execute() && stmt.store_result()) {
                char status_buf[21]; unsigned long status_len = 0;
                MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = status_buf; res_bind[0].buffer_length = sizeof(status_buf); res_bind[0].length = &status_len;
                
                if (stmt.bind_result(res_bind) && !stmt.fetch()) {
                    std::string status(status_buf, status_len);
                    if (status == "pending") {
                        const char* update_query = "UPDATE batches SET status = 'awaiting' WHERE id = ?";
                        utils::StatementWrapper u_stmt(conn);
                        if (u_stmt.isValid() && u_stmt.prepare(update_query)) {
                            u_stmt.bind_params(bind);
                            u_stmt.execute();
                        }
                    }
                }
            }
        }
    }

    CROW_LOG_INFO << "Started batch (moved to awaiting): " << batch_id;
    
    start_monitors();
    
    return true;
}

void BatchManager::recover_stuck_tasks() {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    std::lock_guard<std::mutex> lock(db_mutex_);
    const char* query = "UPDATE tasks SET status = 'pending' WHERE status IN ('processing', 'downloading')";
    utils::StatementWrapper stmt(conn);
    if (!stmt.isValid() || !stmt.prepare(query) || !stmt.execute()) {
        CROW_LOG_ERROR << "Failed to recover stuck tasks: " << stmt.last_error();
    } else {
        unsigned long long affected = stmt.affected_rows();
        if (affected > 0) {
            CROW_LOG_INFO << "Recovered " << affected << " stuck tasks.";
        }
    }
}

void BatchManager::start_monitors() {
    static std::once_flag monitor_started;
    std::call_once(monitor_started, [this]() {
        // Task Processing Monitor
        monitor_threads_.emplace_back([this]() {
            mysql_thread_init();
            CROW_LOG_INFO << "Task Processing Monitor started.";
            while (!stop_flag_) {
                {
                    std::unique_lock<std::mutex> lock(cv_mutex_);
                    task_cv_.wait_for(lock, std::chrono::seconds(5), [this] { return stop_flag_.load(); });
                }
                if (stop_flag_) break;

                MYSQL* conn = DatabaseService::get_instance().get_connection();
                struct BatchInfo { std::string id; long long wait_ms; };
                std::vector<BatchInfo> awaiting_batches;

                {
                    std::lock_guard<std::mutex> lock(db_mutex_);
                    utils::StatementWrapper stmt(conn);
                    if (stmt.prepare("SELECT id, options FROM batches WHERE status = 'awaiting'") && stmt.execute() && stmt.store_result()) {
                        char id_buf[37]; unsigned long id_len;
                        std::vector<char> opt_buf(65536); unsigned long opt_len;
                        MYSQL_BIND res_bind[2]; memset(res_bind, 0, sizeof(res_bind));
                        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
                        res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = opt_buf.data(); res_bind[1].buffer_length = opt_buf.size(); res_bind[1].length = &opt_len;
                        
                        if (stmt.bind_result(res_bind)) {
                            while (stmt.fetch() == 0) {
                                std::string b_id(id_buf, id_len);
                                json opts = parse_batch_options(std::string(opt_buf.data(), opt_len));
                                awaiting_batches.push_back({b_id, utils::parse_duration_ms(opts.value("wait_duration", "5s")).count()});
                            }
                        }
                    }
                }

                for (const auto& b : awaiting_batches) {
                    if (stop_flag_) break;
                    struct TaskInfo { std::string id, file_id, content_type, dest; };
                    std::vector<TaskInfo> tasks_to_do;
                    {
                        std::lock_guard<std::mutex> lock(db_mutex_);
                        utils::StatementWrapper stmt(conn);
                        if (stmt.prepare("SELECT id, file_id, content_type, destination_path, created_at FROM tasks WHERE batch_id = ? AND status = 'pending' ORDER BY created_at ASC")) {
                            MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                            stmt.bind_string_param(0, b.id, bind);
                            if (stmt.bind_params(bind) && stmt.execute() && stmt.store_result()) {
                                char id_buf[37]; unsigned long id_len;
                                std::vector<char> f_id_buf(2048); unsigned long f_id_len;
                                char ct_buf[256]; unsigned long ct_len; bool ct_null;
                                std::vector<char> dest_buf(2048); unsigned long dest_len;
                                char created_buf[30]; unsigned long created_len;
                                
                                MYSQL_BIND res_bind[5]; memset(res_bind, 0, sizeof(res_bind));
                                res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
                                res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = f_id_buf.data(); res_bind[1].buffer_length = f_id_buf.size(); res_bind[1].length = &f_id_len;
                                res_bind[2].buffer_type = MYSQL_TYPE_STRING; res_bind[2].buffer = ct_buf; res_bind[2].buffer_length = sizeof(ct_buf); res_bind[2].length = &ct_len; res_bind[2].is_null = (char*)&ct_null;
                                res_bind[3].buffer_type = MYSQL_TYPE_STRING; res_bind[3].buffer = dest_buf.data(); res_bind[3].buffer_length = dest_buf.size(); res_bind[3].length = &dest_len;
                                res_bind[4].buffer_type = MYSQL_TYPE_STRING; res_bind[4].buffer = created_buf; res_bind[4].buffer_length = sizeof(created_buf); res_bind[4].length = &created_len;
                                
                                if (stmt.bind_result(res_bind) && stmt.fetch() == 0) {
                                    std::string oldest_created(created_buf, created_len);
                                    double elapsed_ms = 0;
                                    utils::StatementWrapper t_stmt(conn);
                                    if (t_stmt.prepare("SELECT (UNIX_TIMESTAMP(NOW()) - UNIX_TIMESTAMP(?)) * 1000")) {
                                        MYSQL_BIND t_bind[1]; memset(t_bind, 0, sizeof(t_bind));
                                        t_stmt.bind_string_param(0, oldest_created, t_bind);
                                        if (t_stmt.bind_params(t_bind) && t_stmt.execute() && t_stmt.store_result()) {
                                            MYSQL_BIND r_bind[1]; memset(r_bind, 0, sizeof(r_bind));
                                            r_bind[0].buffer_type = MYSQL_TYPE_DOUBLE; r_bind[0].buffer = &elapsed_ms;
                                            if (t_stmt.bind_result(r_bind)) t_stmt.fetch();
                                        }
                                    }

                                    if (elapsed_ms >= b.wait_ms) {
                                        std::vector<std::string> ids_to_mark;
                                        do {
                                            tasks_to_do.push_back({std::string(id_buf, id_len), std::string(f_id_buf.data(), f_id_len), ct_null ? "" : std::string(ct_buf, ct_len), std::string(dest_buf.data(), dest_len)});
                                            ids_to_mark.push_back(std::string(id_buf, id_len));
                                        } while (stmt.fetch() == 0);

                                        if (!ids_to_mark.empty() && !stop_flag_) {
                                            for (const auto& task_id : ids_to_mark) {
                                                utils::StatementWrapper u_stmt(conn);
                                                if (u_stmt.prepare("UPDATE tasks SET status = 'processing' WHERE id = ?")) {
                                                    MYSQL_BIND u_bind[1]; memset(u_bind, 0, sizeof(u_bind));
                                                    u_stmt.bind_string_param(0, task_id, u_bind);
                                                    u_stmt.bind_params(u_bind); u_stmt.execute();
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (!tasks_to_do.empty() && !stop_flag_) {
                        std::lock_guard<std::mutex> t_lock(thread_mutex_);
                        monitor_threads_.emplace_back([this, tasks_to_do, b_id = b.id]() mutable {
                            mysql_thread_init();
                            MYSQL* t_conn = DatabaseService::get_instance().get_connection();
                            int max_retries = 3; long long max_bytes = 0; long long inter_task_delay_ms = 5000;
                            json opts;
                            {
                                std::lock_guard<std::mutex> lock(db_mutex_);
                                utils::StatementWrapper stmt(t_conn);
                                if (stmt.prepare("SELECT options FROM batches WHERE id = ?")) {
                                    MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                    stmt.bind_string_param(0, b_id, bind);
                                    if (stmt.bind_params(bind) && stmt.execute() && stmt.store_result()) {
                                        std::vector<char> opt_buf(65536); unsigned long opt_len;
                                        MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                                        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = opt_buf.data(); res_bind[0].buffer_length = opt_buf.size(); res_bind[0].length = &opt_len;
                                        if (stmt.bind_result(res_bind) && !stmt.fetch()) {
                                            opts = parse_batch_options(std::string(opt_buf.data(), opt_len));
                                            max_retries = opts.value("max_retries", 3);
                                            max_bytes = utils::parse_size_string(opts.value("max_batch_storage", "0"));
                                            inter_task_delay_ms = utils::parse_duration_ms(opts.value("wait_duration", "5s")).count();
                                        }
                                    }
                                }
                            }

                            for (size_t i = 0; i < tasks_to_do.size(); ++i) {
                                if (stop_flag_) break;
                                auto t = tasks_to_do[i];
                                {
                                    std::lock_guard<std::mutex> lock(db_mutex_);
                                    utils::StatementWrapper stmt(t_conn);
                                    if (stmt.prepare("UPDATE tasks SET status = 'downloading' WHERE id = ?")) {
                                        MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                        stmt.bind_string_param(0, t.id, bind);
                                        stmt.bind_params(bind); stmt.execute();
                                    }
                                }
                                
                                bool success = false; int attempts = 0;
                                std::string detected_ct = "";
                                while (attempts <= max_retries && !stop_flag_) {
                                    long long allowed_bytes = max_bytes;
                                    if (max_bytes > 0) {
                                        long long current_batch_size = 0;
                                        {
                                            std::lock_guard<std::mutex> lock(db_mutex_);
                                            utils::StatementWrapper stmt(t_conn);
                                            if (stmt.prepare("SELECT SUM(m.file_size) FROM media_metadata m JOIN tasks t ON m.task_id = t.id WHERE t.batch_id = ?")) {
                                                MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                                                stmt.bind_string_param(0, b_id, bind);
                                                if (stmt.bind_params(bind) && stmt.execute() && stmt.store_result()) {
                                                    MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                                                    res_bind[0].buffer_type = MYSQL_TYPE_LONGLONG; res_bind[0].buffer = &current_batch_size;
                                                    stmt.bind_result(res_bind); stmt.fetch();
                                                }
                                            }
                                        }
                                        allowed_bytes = max_bytes - current_batch_size;
                                        if (allowed_bytes <= 0) break;
                                    }

                                    DownloadResult dl_res = DownloadService::download_file(t.file_id, t.dest, allowed_bytes);
                                    if (dl_res.success) {
                                        detected_ct = utils::detect_content_type(t.dest);
                                        if (detected_ct.empty()) {
                                            std::filesystem::remove(t.dest);
                                            CROW_LOG_ERROR << "Download rejected for task " << t.id << ": Unknown magic bytes";
                                            success = false;
                                            break;
                                        }

                                        std::vector<std::string> allowed_ct = opts.value("allowed_content_types", std::vector<std::string>{});
                                        if (!allowed_ct.empty()) {
                                            bool is_allowed = false;
                                            for (const auto& act : allowed_ct) { if (act == detected_ct) { is_allowed = true; break; } }
                                            if (!is_allowed) {
                                                std::filesystem::remove(t.dest);
                                                CROW_LOG_ERROR << "Download rejected for task " << t.id << ": Content-type '" << detected_ct << "' not allowed";
                                                success = false;
                                                break;
                                            }
                                        }

                                        t.content_type = detected_ct;
                                        success = true;
                                        break;
                                    }
                                    attempts++;
                                    if (attempts <= max_retries && !stop_flag_) {
                                        int wait_time = dl_res.rate_limited ? dl_res.retry_after_seconds : 2;
                                        std::this_thread::sleep_for(std::chrono::seconds(wait_time));
                                    }
                                }

                                std::string final_status = success ? "success" : "failed";
                                {
                                    std::lock_guard<std::mutex> lock(db_mutex_);
                                    utils::StatementWrapper stmt(t_conn);
                                    if (stmt.prepare("UPDATE tasks SET status = ?, local_url = ?, content_type = ? WHERE id = ?")) {
                                        MYSQL_BIND bind[4]; memset(bind, 0, sizeof(bind));
                                        std::string local_url_str = success ? "file://" + t.dest : "";
                                        stmt.bind_string_param(0, final_status, bind);
                                        stmt.bind_string_param(1, local_url_str, bind);
                                        stmt.bind_string_param(2, t.content_type, bind);
                                        stmt.bind_string_param(3, t.id, bind);
                                        stmt.bind_params(bind); stmt.execute();
                                    }
                                }

                                if (success && !stop_flag_) {
                                    int res_val = opts.value("waveform_resolution", 512);
                                    MediaResult m_res = MediaService::extract_waveform(t.dest, res_val);
                                    if (m_res.success) {
                                        std::lock_guard<std::mutex> lock(db_mutex_);
                                        utils::StatementWrapper m_stmt(t_conn);
                                        if (m_stmt.prepare("INSERT INTO media_metadata (task_id, file_size, format, duration_seconds) VALUES (?, ?, ?, ?) ON DUPLICATE KEY UPDATE file_size=VALUES(file_size), format=VALUES(format), duration_seconds=VALUES(duration_seconds)")) {
                                            MYSQL_BIND m_bind[4]; memset(m_bind, 0, sizeof(m_bind));
                                            m_stmt.bind_string_param(0, t.id, m_bind);
                                            m_stmt.bind_long_param(1, &m_res.file_size, m_bind);
                                            m_stmt.bind_string_param(2, m_res.format, m_bind);
                                            m_stmt.bind_float_param(3, &m_res.duration_seconds, m_bind);
                                            m_stmt.bind_params(m_bind); m_stmt.execute();
                                        }

                                        const char* wave_query = "INSERT INTO waveforms (task_id, waveform_peaks_binary, resolution) VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE waveform_peaks_binary=VALUES(waveform_peaks_binary), resolution=VALUES(resolution)";
                                        utils::StatementWrapper w_stmt(t_conn);
                                        if (w_stmt.isValid() && w_stmt.prepare(wave_query)) {
                                            MYSQL_BIND bind[3]; memset(bind, 0, sizeof(bind));
                                            w_stmt.bind_string_param(0, t.id, bind);
                                            size_t binary_size = m_res.waveform_peaks.size() * sizeof(services::WaveformPointInt16);
                                            w_stmt.bind_blob_param(1, m_res.waveform_peaks.data(), (unsigned long)binary_size, bind);
                                            w_stmt.bind_int_param(2, &res_val, bind);
                                            w_stmt.bind_params(bind); w_stmt.execute();
                                        }

                                        // 3. Perform Transcoding
                                        std::vector<std::string> trans_formats = opts.value("transcode_formats", std::vector<std::string>{});
                                        if (!trans_formats.empty()) {
                                            std::vector<std::string> successful_transcodes;
                                            std::filesystem::path base_dest = std::filesystem::path(t.dest).parent_path();
                                            for (const auto& fmt : trans_formats) {
                                                if (stop_flag_) break;
                                                std::string out_path = (base_dest / (t.id + "." + fmt)).string();
                                                if (MediaService::transcode_audio(t.dest, out_path, fmt)) {
                                                    successful_transcodes.push_back(fmt);
                                                }
                                            }
                                            
                                            if (!successful_transcodes.empty()) {
                                                std::lock_guard<std::mutex> lock(db_mutex_);
                                                utils::StatementWrapper t_stmt(t_conn);
                                                if (t_stmt.prepare("UPDATE tasks SET transcoded_formats = ? WHERE id = ?")) {
                                                    MYSQL_BIND t_bind[2]; memset(t_bind, 0, sizeof(t_bind));
                                                    std::string trans_json = json(successful_transcodes).dump();
                                                    t_stmt.bind_string_param(0, trans_json, t_bind);
                                                    t_stmt.bind_string_param(1, t.id, t_bind);
                                                    t_stmt.bind_params(t_bind); t_stmt.execute();
                                                }
                                            }
                                        }
                                    }
                                }
                                if (i < tasks_to_do.size() - 1 && !stop_flag_) {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(inter_task_delay_ms));
                                }
                            }
                            mysql_thread_end();
                        });
                    }
                }
            }
            mysql_thread_end();
        });

        // Cleanup Monitor
        monitor_threads_.emplace_back([this]() {
            mysql_thread_init();
            CROW_LOG_INFO << "Cleanup Monitor started.";
            while (!stop_flag_) {
                {
                    std::unique_lock<std::mutex> lock(cv_mutex_);
                    task_cv_.wait_for(lock, std::chrono::seconds(60), [this] { return stop_flag_.load(); });
                }
                if (stop_flag_) break;

                MYSQL* conn = DatabaseService::get_instance().get_connection();
                std::vector<std::string> to_delete;
                {
                    std::lock_guard<std::mutex> lock(db_mutex_);
                    utils::StatementWrapper stmt(conn);
                    if (stmt.prepare("SELECT id FROM batches WHERE status = 'completed' AND delete_at IS NOT NULL AND delete_at <= NOW()") && stmt.execute() && stmt.store_result()) {
                        char id_buf[37]; unsigned long id_len;
                        MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
                        if (stmt.bind_result(res_bind)) {
                            while (stmt.fetch() == 0) to_delete.push_back(std::string(id_buf, id_len));
                        }
                    }
                }

                for (const auto& b_id : to_delete) {
                    if (stop_flag_) break;
                    std::filesystem::path base_dir = storage_dir_;
                    try {
                        if (std::filesystem::exists(base_dir / b_id)) std::filesystem::remove_all(base_dir / b_id);
                        std::lock_guard<std::mutex> lock(db_mutex_);
                        utils::StatementWrapper stmt(conn);
                        if (stmt.prepare("UPDATE batches SET status = 'deleted', delete_at = NULL WHERE id = ?")) {
                            MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
                            stmt.bind_string_param(0, b_id, bind);
                            stmt.bind_params(bind); stmt.execute();
                        }
                    } catch (...) {}
                }
            }
            mysql_thread_end();
        });
    });
}

bool BatchManager::complete_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    long long delete_after_seconds = 0;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* check_query = "SELECT options FROM batches WHERE id = ?";
        utils::StatementWrapper stmt(conn);
        if (stmt.isValid() && stmt.prepare(check_query)) {
            MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
            stmt.bind_string_param(0, batch_id, bind);
            if (stmt.bind_params(bind) && stmt.execute()) {
                std::vector<char> opt_buf(65536); unsigned long opt_len;
                MYSQL_BIND res_bind[1]; memset(res_bind, 0, sizeof(res_bind));
                res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = opt_buf.data(); res_bind[0].buffer_length = opt_buf.size(); res_bind[0].length = &opt_len;
                if (stmt.bind_result(res_bind) && !stmt.fetch()) {
                    json opts = parse_batch_options(std::string(opt_buf.data(), opt_len));
                    delete_after_seconds = utils::parse_duration_s(opts.value("delete_after", "0s")).count();
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* query = (delete_after_seconds > 0) ? "UPDATE batches SET status = 'completed', delete_at = DATE_ADD(NOW(), INTERVAL ? SECOND) WHERE id = ? AND status = 'awaiting'" : "UPDATE batches SET status = 'completed' WHERE id = ? AND status = 'awaiting'";
        utils::StatementWrapper stmt(conn);
        if (!stmt.isValid() || !stmt.prepare(query)) return false;
        
        MYSQL_BIND bind[2]; memset(bind, 0, sizeof(bind));
        if (delete_after_seconds > 0) {
            stmt.bind_long_param(0, &delete_after_seconds, bind);
            stmt.bind_string_param(1, batch_id, bind);
        } else {
            stmt.bind_string_param(0, batch_id, bind);
        }
        if (!stmt.bind_params(bind) || !stmt.execute()) return false;
        if (stmt.affected_rows() == 0) return false;
    }
    return true;
}

std::optional<models::Batch> BatchManager::get_batch(const std::string& batch_id) {
    MYSQL* conn = DatabaseService::get_instance().get_connection();
    models::Batch b; b.id = batch_id;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        const char* query = "SELECT status, options FROM batches WHERE id = ?";
        utils::StatementWrapper stmt(conn);
        if (!stmt.isValid() || !stmt.prepare(query)) return std::nullopt;

        MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
        stmt.bind_string_param(0, batch_id, bind);
        
        if (!stmt.bind_params(bind) || !stmt.execute() || !stmt.store_result()) return std::nullopt;

        if (stmt.num_rows() == 0) {
            CROW_LOG_WARNING << "Batch not found: " << batch_id;
            return std::nullopt;
        }

        char status_buf[21]; unsigned long status_len = 0; std::vector<char> opt_buf(65536); unsigned long opt_len = 0;
        MYSQL_BIND res_bind[2]; memset(res_bind, 0, sizeof(res_bind));
        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = status_buf; res_bind[0].buffer_length = sizeof(status_buf); res_bind[0].length = &status_len;
        res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = opt_buf.data(); res_bind[1].buffer_length = opt_buf.size(); res_bind[1].length = &opt_len;
        if (!stmt.bind_result(res_bind) || stmt.fetch()) return std::nullopt;
        
        b.status = std::string(status_buf, status_len);
        json opts = parse_batch_options(std::string(opt_buf.data(), opt_len));
        b.options.wait_duration = opts.value("wait_duration", "5s"); b.options.max_retries = opts.value("max_retries", 0);
        b.options.max_batch_size = opts.value("max_batch_size", 0); b.options.max_batch_storage = opts.value("max_batch_storage", "");
        b.options.allowed_services = opts.value("allowed_services", std::vector<std::string>{});
        b.options.allowed_content_types = opts.value("allowed_content_types", std::vector<std::string>{});
        b.options.delete_after = opts.value("delete_after", ""); b.options.waveform_resolution = opts.value("waveform_resolution", 1024);

        const char* task_query = "SELECT id, status FROM tasks WHERE batch_id = ?";
        utils::StatementWrapper t_stmt(conn);
        if (t_stmt.isValid() && t_stmt.prepare(task_query)) {
            if (t_stmt.bind_params(bind) && t_stmt.execute() && t_stmt.store_result()) {
                MYSQL_BIND res_bind_tasks[2]; memset(res_bind_tasks, 0, sizeof(res_bind_tasks));
                char id_buf[37]; unsigned long id_len = 0;
                char t_s_buf[21]; unsigned long t_s_len = 0;
                
                res_bind_tasks[0].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[0].buffer = id_buf; res_bind_tasks[0].buffer_length = sizeof(id_buf); res_bind_tasks[0].length = &id_len;
                res_bind_tasks[1].buffer_type = MYSQL_TYPE_STRING; res_bind_tasks[1].buffer = t_s_buf; res_bind_tasks[1].buffer_length = sizeof(t_s_buf); res_bind_tasks[1].length = &t_s_len;
                
                if (t_stmt.bind_result(res_bind_tasks)) {
                    while (!t_stmt.fetch()) {
                        models::TaskSummary t;
                        t.id = std::string(id_buf, id_len);
                        t.status = std::string(t_s_buf, t_s_len);
                        b.tasks.push_back(t);
                    }
                }
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
        const char* query = "SELECT t.id, t.file_id, t.content_type, t.destination_path, t.status, t.local_url, t.transcoded_formats, m.file_size, m.format, m.duration_seconds, w.waveform_peaks_binary, w.resolution FROM tasks t LEFT JOIN media_metadata m ON t.id = m.task_id LEFT JOIN waveforms w ON t.id = w.task_id WHERE t.id = ?";
        utils::StatementWrapper stmt(conn);
        if (!stmt.isValid() || !stmt.prepare(query)) return std::nullopt;

        MYSQL_BIND bind[1]; memset(bind, 0, sizeof(bind));
        stmt.bind_string_param(0, task_id, bind);
        
        if (!stmt.bind_params(bind) || !stmt.execute() || !stmt.store_result()) return std::nullopt;

        if (stmt.num_rows() == 0) {
            CROW_LOG_WARNING << "Task not found in DB: " << task_id;
            return std::nullopt;
        }

        MYSQL_BIND res_bind[12]; memset(res_bind, 0, sizeof(res_bind));
        char id_buf[37]; unsigned long id_len = 0; std::vector<char> f_id_buf(2048); unsigned long f_id_len = 0;
        std::vector<char> ct_buf(255); unsigned long ct_len = 0; bool ct_null = false;
        std::vector<char> dest_buf(2048); unsigned long dest_len = 0; char s_buf[21]; unsigned long s_len = 0; 
        std::vector<char> url_buf(2048); unsigned long url_len = 0; bool url_null = false;
        std::vector<char> trans_buf(65536); unsigned long trans_len = 0; bool trans_null = false;
        long long f_size = 0; bool f_size_null = false; char format_buf[11]; unsigned long format_len = 0; bool format_null = false; float dur = 0; bool dur_null = false; std::vector<char> w_peaks_buf(1024*1024); unsigned long w_peaks_len = 0; bool w_peaks_null = false; int res_val = 0; bool res_null = false;
        
        res_bind[0].buffer_type = MYSQL_TYPE_STRING; res_bind[0].buffer = id_buf; res_bind[0].buffer_length = sizeof(id_buf); res_bind[0].length = &id_len;
        res_bind[1].buffer_type = MYSQL_TYPE_STRING; res_bind[1].buffer = f_id_buf.data(); res_bind[1].buffer_length = f_id_buf.size(); res_bind[1].length = &f_id_len;
        res_bind[2].buffer_type = MYSQL_TYPE_STRING; res_bind[2].buffer = ct_buf.data(); res_bind[2].buffer_length = ct_buf.size(); res_bind[2].length = &ct_len; res_bind[2].is_null = (char*)&ct_null;
        res_bind[3].buffer_type = MYSQL_TYPE_STRING; res_bind[3].buffer = dest_buf.data(); res_bind[3].buffer_length = dest_buf.size(); res_bind[3].length = &dest_len;
        res_bind[4].buffer_type = MYSQL_TYPE_STRING; res_bind[4].buffer = s_buf; res_bind[4].buffer_length = sizeof(s_buf); res_bind[4].length = &s_len;
        res_bind[5].buffer_type = MYSQL_TYPE_STRING; res_bind[5].buffer = url_buf.data(); res_bind[5].buffer_length = url_buf.size(); res_bind[5].length = &url_len; res_bind[5].is_null = (char*)&url_null;
        res_bind[6].buffer_type = MYSQL_TYPE_STRING; res_bind[6].buffer = trans_buf.data(); res_bind[6].buffer_length = trans_buf.size(); res_bind[6].length = &trans_len; res_bind[6].is_null = (char*)&trans_null;
        res_bind[7].buffer_type = MYSQL_TYPE_LONGLONG; res_bind[7].buffer = &f_size; res_bind[7].is_null = (char*)&f_size_null;
        res_bind[8].buffer_type = MYSQL_TYPE_STRING; res_bind[8].buffer = format_buf; res_bind[8].buffer_length = sizeof(format_buf); res_bind[8].length = &format_len; res_bind[8].is_null = (char*)&format_null;
        res_bind[9].buffer_type = MYSQL_TYPE_FLOAT; res_bind[9].buffer = &dur; res_bind[9].is_null = (char*)&dur_null;
        res_bind[10].buffer_type = MYSQL_TYPE_BLOB; res_bind[10].buffer = w_peaks_buf.data(); res_bind[10].buffer_length = w_peaks_buf.size(); res_bind[10].length = &w_peaks_len; res_bind[10].is_null = (char*)&w_peaks_null;
        res_bind[11].buffer_type = MYSQL_TYPE_LONG; res_bind[11].buffer = &res_val; res_bind[11].is_null = (char*)&res_null;
        
        if (!stmt.bind_result(res_bind) || stmt.fetch()) return std::nullopt;

        t.id = std::string(id_buf, id_len); t.file_id = std::string(f_id_buf.data(), f_id_len); t.content_type = ct_null ? "" : std::string(ct_buf.data(), ct_len); t.destination_path = std::string(dest_buf.data(), dest_len); t.status = std::string(s_buf, s_len);
        
        // Populate url
        if (!url_null) {
            t.url.push_back({"/v1/ingested/" + t.id + "/stream", std::string(url_buf.data(), url_len), t.content_type});
        }
        
        if (!trans_null) {
            try {
                json transcoded = json::parse(std::string(trans_buf.data(), trans_len));
                if (transcoded.is_array()) {
                    std::filesystem::path base_dest = std::filesystem::path(t.destination_path).parent_path();
                    for (const auto& format : transcoded) {
                        std::string fmt = format.get<std::string>();
                        std::string trans_dest = (base_dest / (t.id + "." + fmt)).string();
                        std::string ct = "audio/" + fmt; // Simplified, could be improved
                        if (fmt == "mp3") ct = "audio/mpeg";
                        else if (fmt == "ogg") ct = "audio/ogg";
                        
                        t.url.push_back({"/v1/ingested/" + t.id + "/stream?format=" + fmt, "file://" + trans_dest, ct});
                    }
                }
            } catch (...) {}
        }

        if (!f_size_null) { models::TaskMetadata m; m.file_size = f_size; m.format = format_null ? "" : std::string(format_buf, format_len); m.duration_seconds = dur_null ? 0 : dur; t.metadata = m; }
        if (!w_peaks_null) { t.waveform_peaks_b64 = utils::base64_encode((const unsigned char*)w_peaks_buf.data(), w_peaks_len); t.waveform_resolution = res_null ? 0 : res_val; }
    }
    return t;
}

} // namespace services
