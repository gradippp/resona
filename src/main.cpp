#include "crow.h"
#include "version.h"
#include "routes/routes.h"
#include "services/database_service.h"
#include "services/batch_manager.h"
#include "utils/logger.h"
#include "utils/response.h"
#include <iostream>
#include <cstdlib>
#include <regex>
#include <stdexcept>

int main() {
    // Set custom logger
    static utils::CustomLogger logger;
    crow::logger::setHandler(&logger);

    // Database initialization with retries
    const char* db_url_env = std::getenv("DATABASE_URL");
    if (!db_url_env) {
        std::cerr << "Error: DATABASE_URL environment variable is not set." << std::endl;
        return 1;
    }

    std::string db_url(db_url_env);
    std::regex url_regex(R"(^mysql://(?:([^:]+)(?::([^@]+))?@)?([^:/]+)(?::(\d+))?/(.+)$)");
    std::smatch url_match;

    if (!std::regex_match(db_url, url_match, url_regex)) {
        std::cerr << "Error: Invalid DATABASE_URL format. Expected: mysql://[user[:pass]@]host[:port]/dbname" << std::endl;
        return 1;
    }

    std::string db_user = url_match[1].matched ? url_match[1].str() : "";
    std::string db_pass = url_match[2].matched ? url_match[2].str() : "";
    std::string db_host = url_match[3].str();
    int db_port = url_match[4].matched ? std::stoi(url_match[4].str()) : 3306;
    std::string db_name = url_match[5].str();

    bool db_ready = false;
    int retries = 0;
    const int max_retries = 30;
    while (!db_ready && retries < max_retries) {
        try {
            services::DatabaseService::get_instance().initialize(db_host.c_str(), db_port, db_user.c_str(), db_pass.c_str(), db_name.c_str());
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

    auto storage_dir = services::BatchManager::get_instance().get_storage_directory();
    CROW_LOG_INFO << "Storage directory: " << storage_dir;

    // Register a global 404 handler
    CROW_CATCHALL_ROUTE(app)([](crow::response& res) {
        res = utils::error_response("The requested resource was not found", 404);
        res.end();
    });

    std::cout << "Resona v" << RESONA_VERSION << " listening on port 8080..." << std::endl;
    app.port(8080).multithreaded().run();
}
