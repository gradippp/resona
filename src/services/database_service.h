#pragma once
#include <mysql.h>
#include <string>
#include <mutex>

namespace services {

class DatabaseService {
public:
    static DatabaseService& get_instance();

    void initialize(const std::string& host, int port, const std::string& user, const std::string& pass, const std::string& db);
    void initialize_schema();
    
    /**
     * Returns a thread-local MySQL connection.
     * This method is thread-safe and will attempt reconnection if the connection is dead.
     */
    MYSQL* get_connection();
    
    /**
     * Manually triggers a reconnection for the current thread's connection.
     */
    bool reconnect();

private:
    DatabaseService();
    ~DatabaseService();
    DatabaseService(const DatabaseService&) = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;

    MYSQL* create_connection();

    std::mutex mutex_;
    
    // Connection params for reconnection
    std::string host_;
    int port_;
    std::string user_;
    std::string pass_;
    std::string db_;
    bool initialized_ = false;
};

} // namespace services
