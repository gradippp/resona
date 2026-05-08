#pragma once
#include <mysql.h>
#include <string>
#include <memory>

namespace services {

class DatabaseService {
public:
    static DatabaseService& get_instance();

    void initialize(const std::string& host, int port, const std::string& user, const std::string& pass, const std::string& db);
    MYSQL* get_connection();

private:
    DatabaseService();
    ~DatabaseService();
    DatabaseService(const DatabaseService&) = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;

    MYSQL* conn_;
};

} // namespace services
