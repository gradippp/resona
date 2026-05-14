#include "database_service.h"
#include "crow/logging.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>

namespace services {

DatabaseService& DatabaseService::get_instance() {
    static DatabaseService instance;
    return instance;
}

DatabaseService::DatabaseService() : port_(0) {}

DatabaseService::~DatabaseService() {
    // Note: Thread-local connections will close via their own destructors
}

void DatabaseService::initialize(const std::string& host, int port, const std::string& user, const std::string& pass, const std::string& db) {
    std::lock_guard<std::mutex> lock(mutex_);
    host_ = host;
    port_ = port;
    user_ = user;
    pass_ = pass;
    db_ = db;
    initialized_ = true;

    CROW_LOG_INFO << "DatabaseService initialized for " << host << ":" << port;
}

struct ThreadLocalConnection {
    MYSQL* conn = nullptr;
    ~ThreadLocalConnection() {
        if (conn) {
            mysql_close(conn);
            CROW_LOG_INFO << "Thread-local database connection closed.";
        }
    }
};

MYSQL* DatabaseService::create_connection() {
    std::string host, user, pass, db;
    int port;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        host = host_;
        user = user_;
        pass = pass_;
        db = db_;
        port = port_;
    }

    MYSQL* conn = mysql_init(nullptr);
    if (!conn) return nullptr;

    bool reconnect = true;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(conn, host.c_str(), user.c_str(), pass.c_str(), db.c_str(), port, nullptr, 0)) {
        CROW_LOG_ERROR << "mysql_real_connect() failed: " << mysql_error(conn);
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

bool DatabaseService::reconnect() {
    static thread_local ThreadLocalConnection tl_conn;
    if (tl_conn.conn) {
        mysql_close(tl_conn.conn);
        tl_conn.conn = nullptr;
    }
    tl_conn.conn = create_connection();
    return tl_conn.conn != nullptr;
}

void DatabaseService::initialize_schema() {
    MYSQL* conn = get_connection();
    
    // Explicitly select the database
    if (mysql_select_db(conn, db_.c_str()) != 0) {
        throw std::runtime_error("Failed to select database: " + std::string(mysql_error(conn)));
    }

    // 1. Ensure the migrations table exists
    const char* create_migrations_table = 
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version INT PRIMARY KEY,"
        "  applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB;";

    if (mysql_query(conn, create_migrations_table)) {
        throw std::runtime_error("Failed to create schema_migrations table: " + std::string(mysql_error(conn)));
    }

    // 2. Get current version
    int current_version = 0;
    if (mysql_query(conn, "SELECT MAX(version) FROM schema_migrations")) {
        throw std::runtime_error("Failed to query schema_migrations: " + std::string(mysql_error(conn)));
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
            current_version = std::stoi(row[0]);
        }
        mysql_free_result(res);
    }

    // 3. Define migrations
    struct Migration {
        int version;
        std::vector<std::string> queries;
    };

    std::vector<Migration> migrations = {
        {1, {
            "CREATE TABLE IF NOT EXISTS batches ("
            "  id VARCHAR(36) PRIMARY KEY,"
            "  status VARCHAR(20) NOT NULL,"
            "  options JSON NOT NULL,"
            "  delete_at DATETIME NULL,"
            "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB;",
            "CREATE TABLE IF NOT EXISTS tasks ("
            "  id VARCHAR(36) PRIMARY KEY,"
            "  batch_id VARCHAR(36) NOT NULL,"
            "  file_id TEXT NOT NULL,"
            "  content_type VARCHAR(255) NULL,"
            "  destination_path TEXT NOT NULL,"
            "  status VARCHAR(20) NOT NULL,"
            "  local_url TEXT,"
            "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
            "  FOREIGN KEY (batch_id) REFERENCES batches(id) ON DELETE CASCADE"
            ") ENGINE=InnoDB;",
            "CREATE TABLE IF NOT EXISTS media_metadata ("
            "  task_id VARCHAR(36) PRIMARY KEY,"
            "  file_size BIGINT,"
            "  format VARCHAR(50),"
            "  duration_seconds FLOAT,"
            "  FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE"
            ") ENGINE=InnoDB;",
            "CREATE TABLE IF NOT EXISTS waveforms ("
            "  task_id VARCHAR(36) PRIMARY KEY,"
            "  waveform_peaks_binary LONGBLOB,"
            "  resolution INT,"
            "  FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE"
            ") ENGINE=InnoDB;"
        }},
        {2, {
            "ALTER TABLE tasks ADD COLUMN IF NOT EXISTS content_type VARCHAR(255) NULL",
            "ALTER TABLE waveforms ADD COLUMN IF NOT EXISTS waveform_peaks_binary LONGBLOB"
        }}
    };

    // 4. Apply migrations
    for (const auto& m : migrations) {
        if (m.version > current_version) {
            CROW_LOG_INFO << "Applying database migration version " << m.version << "...";
            
            for (const auto& query : m.queries) {
                if (mysql_query(conn, query.c_str())) {
                    throw std::runtime_error("Migration version " + std::to_string(m.version) + " failed at query: " + query + ". Error: " + mysql_error(conn));
                }
            }

            // Record migration
            std::string record_query = "INSERT INTO schema_migrations (version) VALUES (" + std::to_string(m.version) + ")";
            if (mysql_query(conn, record_query.c_str())) {
                throw std::runtime_error("Failed to record migration version " + std::to_string(m.version) + ": " + mysql_error(conn));
            }
            
            CROW_LOG_INFO << "Database migration version " << m.version << " applied successfully.";
        }
    }

    CROW_LOG_INFO << "Database schema is up to date (version " << (migrations.empty() ? 0 : migrations.back().version) << ").";
}

MYSQL* DatabaseService::get_connection() {
    if (!initialized_) {
        throw std::runtime_error("Database connection not initialized. Call initialize() first.");
    }

    static thread_local ThreadLocalConnection tl_conn;
    
    if (!tl_conn.conn) {
        tl_conn.conn = create_connection();
        if (!tl_conn.conn) {
            throw std::runtime_error("Failed to create thread-local database connection.");
        }
        CROW_LOG_INFO << "New thread-local database connection established.";
    } else {
        if (mysql_ping(tl_conn.conn) != 0) {
            CROW_LOG_WARNING << "Thread-local database connection lost, reconnecting...";
            if (!reconnect()) {
                throw std::runtime_error("Failed to reconnect thread-local database connection.");
            }
        }
    }

    return tl_conn.conn;
}

} // namespace services
