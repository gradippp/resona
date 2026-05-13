#include <catch2/catch_session.hpp>
#include "services/database_service.h"
#include "utils/logger.h"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    // Set custom logger
    static utils::CustomLogger logger;
    crow::logger::setHandler(&logger);

    auto& db = services::DatabaseService::get_instance();
    int attempts = 0;
    bool connected = false;
    
    while (attempts < 10 && !connected) {
        try {
            db.initialize("127.0.0.1", 3306, "root", "root", "");
            mysql_query(db.get_connection(), "DROP DATABASE IF EXISTS resona");
            mysql_query(db.get_connection(), "CREATE DATABASE resona");
            db.initialize("127.0.0.1", 3306, "root", "root", "resona");
            connected = true;
        } catch (...) {
            attempts++;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (connected) {
        db.initialize_schema();
    } else {
        return 1;
    }

    int result = Catch::Session().run(argc, argv);

    return result;
}
