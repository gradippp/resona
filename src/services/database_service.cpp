#include "database_service.h"
#include "crow/logging.h"
#include <iostream>
#include <stdexcept>

namespace services {

DatabaseService& DatabaseService::get_instance() {
    static DatabaseService instance;
    return instance;
}

DatabaseService::DatabaseService() : conn_(nullptr) {}

DatabaseService::~DatabaseService() {
    if (conn_) {
        mysql_close(conn_);
    }
}

void DatabaseService::initialize(const std::string& host, int port, const std::string& user, const std::string& pass, const std::string& db) {
    CROW_LOG_INFO << "Connecting to MariaDB at " << host << ":" << port << "...";

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        throw std::runtime_error("mysql_init() failed");
    }

    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, nullptr, 0)) {
        std::string error = mysql_error(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        throw std::runtime_error("mysql_real_connect() failed: " + error);
    }

    CROW_LOG_INFO << "Database connection established successfully.";
}

void DatabaseService::initialize_schema() {
    const char* schema_queries[] = {
        "CREATE TABLE IF NOT EXISTS batches ("
        "  id VARCHAR(36) PRIMARY KEY,"
        "  status VARCHAR(20) NOT NULL,"
        "  options JSON,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")",
        "CREATE TABLE IF NOT EXISTS tasks ("
        "  id VARCHAR(36) PRIMARY KEY,"
        "  batch_id VARCHAR(36) NOT NULL,"
        "  file_id TEXT NOT NULL,"
        "  destination_path TEXT NOT NULL,"
        "  status VARCHAR(20) NOT NULL,"
        "  local_url TEXT,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (batch_id) REFERENCES batches(id) ON DELETE CASCADE"
        ")",
        "CREATE TABLE IF NOT EXISTS media_metadata ("
        "  task_id VARCHAR(36) PRIMARY KEY,"
        "  file_size BIGINT,"
        "  format VARCHAR(50),"
        "  duration_seconds FLOAT,"
        "  FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE"
        ")",
        "CREATE TABLE IF NOT EXISTS waveforms ("
        "  task_id VARCHAR(36) PRIMARY KEY,"
        "  waveform_data LONGBLOB,"
        "  resolution INT,"
        "  FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE"
        ")"
    };

    for (const char* query : schema_queries) {
        if (mysql_query(conn_, query)) {
            throw std::runtime_error("Failed to execute schema query: " + std::string(mysql_error(conn_)));
        }
    }

    CROW_LOG_INFO << "Database schema initialized successfully.";
}

MYSQL* DatabaseService::get_connection() {
    if (!conn_) {
        throw std::runtime_error("Database connection not initialized. Call initialize() first.");
    }
    return conn_;
}

} // namespace services
