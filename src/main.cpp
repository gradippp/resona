#include "crow.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;

int main() {
    crow::SimpleApp app;

    // POST /v1/batch - Create a Batch
    CROW_ROUTE(app, "/v1/batch").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        auto x = json::parse(req.body);
        std::cout << "Creating batch with options: " << x.dump() << std::endl;
        
        json response;
        response["batch_id"] = "uuid-v4-stub-1234";
        response["status"] = "pending";
        
        return crow::response(response.dump());
    });

    // POST /v1/batch/{batch_id} - Add a Download Task
    CROW_ROUTE(app, "/v1/batch/<string>").methods(crow::HTTPMethod::POST)([](std::string batch_id) {
        std::cout << "Adding task to batch: " << batch_id << std::endl;
        return crow::response(202, "Task queued in batch " + batch_id);
    });

    // POST /v1/batch/{batch_id}/start - Start a Batch
    CROW_ROUTE(app, "/v1/batch/<string>/start").methods(crow::HTTPMethod::POST)([](std::string batch_id) {
        std::cout << "Starting batch: " << batch_id << std::endl;
        return crow::response(200, "Batch " + batch_id + " started");
    });

    // GET /v1/batch/{batch_id} - Check Batch Status
    CROW_ROUTE(app, "/v1/batch/<string>").methods(crow::HTTPMethod::GET)([](std::string batch_id) {
        json response;
        response["batch_id"] = batch_id;
        response["status"] = "completed";
        response["tasks"] = json::array({
            {{"file_id", "file-1"}, {"status", "success"}, {"local_url", "/a1b2c3d4-stub.zip"}}
        });
        
        return crow::response(response.dump());
    });

    std::cout << "Cloud Download Service listening on port 8080..." << std::endl;
    app.port(8080).multithreaded().run();
}
