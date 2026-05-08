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

MYSQL* DatabaseService::get_connection() {
    if (!conn_) {
        throw std::runtime_error("Database connection not initialized. Call initialize() first.");
    }
    return conn_;
}

} // namespace services
