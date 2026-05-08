#include "crow.h"
#include "version.h"
#include "routes/routes.h"
#include "services/database_service.h"
#include "utils/logger.h"
#include <iostream>
#include <cstdlib>

int main() {
    // Set custom logger
    static utils::CustomLogger logger;
    crow::logger::setHandler(&logger);

    // Database initialization
    const char* db_host = std::getenv("DB_HOST") ? std::getenv("DB_HOST") : "localhost";
    const char* db_port_str = std::getenv("DB_PORT") ? std::getenv("DB_PORT") : "3306";
    const char* db_user = std::getenv("DB_USER") ? std::getenv("DB_USER") : "strata_user";
    const char* db_pass = std::getenv("DB_PASS") ? std::getenv("DB_PASS") : "strata_password";
    const char* db_name = std::getenv("DB_NAME") ? std::getenv("DB_NAME") : "strata";

    try {
        int db_port = std::stoi(db_port_str);
        services::DatabaseService::get_instance().initialize(db_host, db_port, db_user, db_pass, db_name);
    } catch (const std::exception& e) {
        CROW_LOG_WARNING << "Database initialization failed: " << e.what();
        CROW_LOG_WARNING << "Service will continue, but database-dependent features may fail.";
    }

    crow::SimpleApp app;

    // Setup routes
    routes::setup(app);

    std::cout << "Strata v" << STRATA_VERSION << " listening on port 8080..." << std::endl;
    app.port(8080).multithreaded().run();
}
