#include <catch2/catch_session.hpp>
#include "services/database_service.h"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char* argv[]) {
    std::cout << "GLOBAL SETUP: Initializing database for tests..." << std::endl;
    
    auto& db = services::DatabaseService::get_instance();
    int attempts = 0;
    bool connected = false;
    
    while (attempts < 10 && !connected) {
        try {
            db.initialize("127.0.0.1", 3306, "root", "root", "resona");
            connected = true;
        } catch (...) {
            try {
                db.initialize("127.0.0.1", 3306, "root", "root", "");
                mysql_query(db.get_connection(), "CREATE DATABASE IF NOT EXISTS resona");
                db.initialize("127.0.0.1", 3306, "root", "root", "resona");
                connected = true;
            } catch (const std::exception& e) {
                attempts++;
                std::cout << "GLOBAL SETUP: Connection attempt " << attempts << " failed: " << e.what() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }

    if (connected) {
        db.initialize_schema();
        std::cout << "GLOBAL SETUP: Database initialized successfully." << std::endl;
    } else {
        std::cerr << "GLOBAL SETUP: Failed to initialize database after " << attempts << " attempts." << std::endl;
        return 1;
    }

    int result = Catch::Session().run(argc, argv);

    return result;
}
