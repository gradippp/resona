#include "batch.h"
#include "../models/batch.h"
#include "../services/batch_manager.h"
#include "../utils/response.h"
#include <iostream>

namespace routes {
namespace batch {

void setup(crow::SimpleApp& app) {
    auto& manager = services::BatchManager::get_instance();

    // POST /v1/batch - Create a Batch
    CROW_ROUTE(app, "/v1/batch").methods(crow::HTTPMethod::POST)([&manager](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            auto create_options = body.get<models::CreateBatchRequest>();
            
            std::string batch_id = manager.create_batch(create_options);
            
            nlohmann::json response;
            response["batch_id"] = batch_id;
            response["status"] = "pending";
            
            return utils::json_response(response);
        } catch (const std::exception& e) {
            return utils::error_response("Invalid JSON or missing fields: " + std::string(e.what()));
        }
    });

    // POST /v1/batch/{batch_id} - Add a Download Task
    CROW_ROUTE(app, "/v1/batch/<string>").methods(crow::HTTPMethod::POST)([&manager](const crow::request& req, std::string batch_id) {
        try {
            auto body = nlohmann::json::parse(req.body);
            auto task_req = body.get<models::AddTaskRequest>();
            
            if (manager.add_task(batch_id, task_req)) {
                nlohmann::json response;
                response["message"] = "Task queued in batch " + batch_id;
                return utils::json_response(response, 202);
            } else {
                return utils::error_response("Batch not found or already started", 404);
            }
        } catch (const std::exception& e) {
            return utils::error_response("Invalid JSON: " + std::string(e.what()));
        }
    });

    // POST /v1/batch/{batch_id}/start - Start a Batch
    CROW_ROUTE(app, "/v1/batch/<string>/start").methods(crow::HTTPMethod::POST)([&manager](std::string batch_id) {
        if (manager.start_batch(batch_id)) {
            nlohmann::json response;
            response["message"] = "Batch " + batch_id + " started";
            return utils::json_response(response);
        } else {
            return utils::error_response("Batch not found or already started", 404);
        }
    });

    // GET /v1/batch/{batch_id} - Check Batch Status
    CROW_ROUTE(app, "/v1/batch/<string>").methods(crow::HTTPMethod::GET)([&manager](std::string batch_id) {
        auto batch = manager.get_batch(batch_id);
        if (batch) {
            return utils::json_response(*batch);
        } else {
            return utils::error_response("Batch not found", 404);
        }
    });
}

} // namespace batch
} // namespace routes
