#include "batch.h"
#include "../models/batch.h"
#include "../utils/response.h"
#include <iostream>

namespace routes {
namespace batch {

void setup(crow::SimpleApp& app) {
    // POST /v1/batch - Create a Batch
    CROW_ROUTE(app, "/v1/batch").methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            auto create_options = body.get<models::CreateBatchRequest>();
            
            std::cout << "Creating batch with wait_duration: " << create_options.wait_duration << std::endl;
            
            nlohmann::json response;
            response["batch_id"] = "uuid-v4-stub-1234";
            response["status"] = "pending";
            
            return utils::json_response(response);
        } catch (const std::exception& e) {
            return utils::error_response("Invalid JSON or missing fields: " + std::string(e.what()));
        }
    });

    // POST /v1/batch/{batch_id} - Add a Download Task
    CROW_ROUTE(app, "/v1/batch/<string>").methods(crow::HTTPMethod::POST)([](std::string batch_id) {
        std::cout << "Adding task to batch: " << batch_id << std::endl;
        nlohmann::json response;
        response["message"] = "Task queued in batch " + batch_id;
        return utils::json_response(response, 202);
    });

    // POST /v1/batch/{batch_id}/start - Start a Batch
    CROW_ROUTE(app, "/v1/batch/<string>/start").methods(crow::HTTPMethod::POST)([](std::string batch_id) {
        std::cout << "Starting batch: " << batch_id << std::endl;
        nlohmann::json response;
        response["message"] = "Batch " + batch_id + " started";
        return utils::json_response(response);
    });

    // GET /v1/batch/{batch_id} - Check Batch Status
    CROW_ROUTE(app, "/v1/batch/<string>").methods(crow::HTTPMethod::GET)([](std::string batch_id) {
        nlohmann::json response;
        response["batch_id"] = batch_id;
        response["status"] = "completed";
        response["tasks"] = nlohmann::json::array({
            {{"file_id", "file-1"}, {"status", "success"}, {"local_url", "/a1b2c3d4-stub.zip"}}
        });
        
        return utils::json_response(response);
    });
}

} // namespace batch
} // namespace routes
