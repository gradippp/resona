#include "batch.h"
#include "../models/batch.h"
#include "../services/batch_manager.h"
#include "../utils/response.h"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace routes {
namespace batch {

void setup(crow::SimpleApp& app) {
    auto& manager = services::BatchManager::get_instance();

    // POST /v1/batch - Create a Batch
    CROW_ROUTE(app, "/v1/batch").methods(crow::HTTPMethod::POST)([&manager](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            
            if (!body.contains("delete_after") || body["delete_after"].get<std::string>().empty()) {
                return utils::error_response("'delete_after' is a required field and cannot be empty", 400);
            }

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

    // POST /v1/batch/{batch_id}/complete - Mark a Batch as Complete
    CROW_ROUTE(app, "/v1/batch/<string>/complete").methods(crow::HTTPMethod::POST)([&manager](std::string batch_id) {
        if (manager.complete_batch(batch_id)) {
            nlohmann::json response;
            response["batch_id"] = batch_id;
            response["status"] = "completed";
            return utils::json_response(response);
        } else {
            return utils::error_response("Batch not found or not in awaiting state", 404);
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

    // GET /v1/ingested/{task_id}/stream - Stream the ingested file with seeking support
    CROW_ROUTE(app, "/v1/ingested/<string>/stream").methods(crow::HTTPMethod::GET)([&manager](const crow::request& req, crow::response& res, std::string task_id) {
        auto task = manager.get_ingested_task(task_id);
        if (!task) {
            auto err = utils::error_response("Ingested task not found", 404);
            res.code = err.code;
            res.body = std::move(err.body);
            for(auto& h : err.headers) res.set_header(h.first, h.second);
            res.end();
            return;
        }

        if (task->status != "success") {
            auto err = utils::error_response("Task has not completed successfully", 400);
            res.code = err.code;
            res.body = std::move(err.body);
            for(auto& h : err.headers) res.set_header(h.first, h.second);
            res.end();
            return;
        }

        std::string path = task->destination_path;
        if (!std::filesystem::exists(path)) {
            auto err = utils::error_response("File not found on disk", 404);
            res.code = err.code;
            res.body = std::move(err.body);
            for(auto& h : err.headers) res.set_header(h.first, h.second);
            res.end();
            return;
        }

        std::string range_header = req.get_header_value("Range");
        if (range_header.empty()) {
            res.set_static_file_info(path);
            res.add_header("Accept-Ranges", "bytes");
            res.end();
            return;
        }

        // Simple range parsing: "bytes=start-end"
        size_t file_size = std::filesystem::file_size(path);
        size_t start = 0, end = file_size - 1;

        try {
            std::string prefix = "bytes=";
            if (range_header.substr(0, prefix.size()) == prefix) {
                std::string range = range_header.substr(prefix.size());
                size_t dash_pos = range.find('-');
                if (dash_pos != std::string::npos) {
                    std::string s_start = range.substr(0, dash_pos);
                    std::string s_end = range.substr(dash_pos + 1);
                    if (!s_start.empty()) start = std::stoull(s_start);
                    if (!s_end.empty()) end = std::stoull(s_end);
                }
            }
        } catch (...) {
            auto err = utils::error_response("Invalid Range header", 400);
            res.code = err.code;
            res.body = std::move(err.body);
            for(auto& h : err.headers) res.set_header(h.first, h.second);
            res.end();
            return;
        }

        if (start >= file_size || end >= file_size || start > end) {
            res.code = 416;
            res.add_header("Content-Range", "bytes */" + std::to_string(file_size));
            res.end();
            return;
        }

        size_t length = end - start + 1;
        std::ifstream file(path, std::ios::binary);
        file.seekg(start);

        std::string buffer(length, '\0');
        file.read(&buffer[0], length);

        res.code = 206;
        res.body = std::move(buffer);
        res.add_header("Content-Range", "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size));
        res.add_header("Content-Length", std::to_string(length));
        res.add_header("Accept-Ranges", "bytes");
        
        std::size_t last_dot = path.find_last_of('.');
        if (last_dot != std::string::npos) {
            res.add_header("Content-Type", crow::response::get_mime_type(path.substr(last_dot + 1)));
        }

        res.end();
    });

    // GET /v1/ingested/{task_id} - Get specific ingested file metadata and waveform
    CROW_ROUTE(app, "/v1/ingested/<string>").methods(crow::HTTPMethod::GET)([&manager](std::string task_id) {
        auto task = manager.get_ingested_task(task_id);
        if (task) {
            return utils::json_response(*task);
        } else {
            return utils::error_response("Ingested task not found", 404);
        }
    });

}

} // namespace batch
} // namespace routes
