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

    // Database initialization with retries
    const char* db_host = std::getenv("DB_HOST") ? std::getenv("DB_HOST") : "localhost";
    const char* db_port_str = std::getenv("DB_PORT") ? std::getenv("DB_PORT") : "3306";
    const char* db_user = std::getenv("DB_USER") ? std::getenv("DB_USER") : "resona_user";
    const char* db_pass = std::getenv("DB_PASS") ? std::getenv("DB_PASS") : "resona_password";
    const char* db_name = std::getenv("DB_NAME") ? std::getenv("DB_NAME") : "resona";

    bool db_ready = false;
    int retries = 0;
    const int max_retries = 30;
    while (!db_ready && retries < max_retries) {
        try {
            int db_port = std::stoi(db_port_str);
            services::DatabaseService::get_instance().initialize(db_host, db_port, db_user, db_pass, db_name);
            services::DatabaseService::get_instance().initialize_schema();
            db_ready = true;
            CROW_LOG_INFO << "Database initialized successfully.";
        } catch (const std::exception& e) {
            retries++;
            CROW_LOG_WARNING << "Database initialization attempt " << retries << "/" << max_retries << " failed: " << e.what();
            if (retries < max_retries) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    if (!db_ready) {
        CROW_LOG_ERROR << "Failed to initialize database after " << max_retries << " attempts. Service will continue, but database-dependent features will fail.";
    }

    crow::SimpleApp app;

    // Setup routes
    routes::setup(app);

    std::cout << "Resona v" << RESONA_VERSION << " listening on port 8080..." << std::endl;
    app.port(8080).multithreaded().run();
}
