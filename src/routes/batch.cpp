#include "batch.h"
#include "../models/batch.h"
#include "../services/batch_manager.h"
#include "../utils/response.h"
#include "../utils/path_utils.h"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace routes {
namespace batch {

    void setup(crow::SimpleApp& app) {
        auto& manager = services::BatchManager::get_instance();

        // POST /v1/batch - Create a Batch
        CROW_ROUTE(app, "/v1/batch").methods(crow::HTTPMethod::POST)([&manager](const crow::request& req, crow::response& res) {
            auto body = utils::parse_json_or_error(req, res);
            if (body.is_null()) return;

            if (!body.contains("delete_after") || body["delete_after"].get<std::string>().empty()) {
                utils::send_error(res, "'delete_after' is a required field and cannot be empty", 400);
                return;
            }

            try {
                auto create_options = body.get<models::CreateBatchRequest>();
                std::string batch_id = manager.create_batch(create_options);

                nlohmann::json response;
                response["batch_id"] = batch_id;
                response["status"] = "pending";

                res = utils::json_response(response);
                res.end();
            } catch (const std::exception& e) {
                utils::send_error(res, "Missing or invalid fields: " + std::string(e.what()));
            }
        });

        // POST /v1/batch/{batch_id} - Add a Download Task
        CROW_ROUTE(app, "/v1/batch/<string>").methods(crow::HTTPMethod::POST)([&manager](const crow::request& req, crow::response& res, std::string batch_id) {
            auto body = utils::parse_json_or_error(req, res);
            if (body.is_null()) return;

            try {
                auto task_req = body.get<models::AddTaskRequest>();

                auto task_id = manager.add_task(batch_id, task_req);
                if (task_id) {
                    nlohmann::json response;
                    response["id"] = *task_id;
                    res = utils::json_response(response, 202);
                } else {
                    res = utils::error_response("Batch not found or already started", 404);
                }
                res.end();
            } catch (const std::exception& e) {
                utils::send_error(res, "Invalid Task format: " + std::string(e.what()));
            }
        });

        // POST /v1/batch/{batch_id}/start - Start a Batch
        CROW_ROUTE(app, "/v1/batch/<string>/start").methods(crow::HTTPMethod::POST)([&manager](std::string batch_id) {
            if (manager.start_batch(batch_id)) {
                return crow::response(200);
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
        CROW_ROUTE(app, "/v1/ingested/<string>/stream").methods(crow::HTTPMethod::GET)([&manager](const crow::request& req, std::string task_id) {
            CROW_LOG_INFO << "Streaming request for task: " << task_id;

            auto task = manager.get_ingested_task(task_id);
            if (!task) {
                CROW_LOG_WARNING << "Streaming failed: Task " << task_id << " not found";
                return utils::error_response("Ingested task not found", 404);
            }

            if (task->status != "success") {
                CROW_LOG_WARNING << "Streaming failed: Task " << task_id << " status is " << task->status;
                return utils::error_response("Task has not completed successfully", 400);
            }

            std::string path = task->destination_path;

            // Check for format override
            char* format_param = req.url_params.get("format");
            if (format_param) {
                std::string fmt(format_param);
                std::filesystem::path base_path = std::filesystem::path(path).parent_path();
                std::string trans_path = (base_path / (task_id + "." + fmt)).string();
                if (std::filesystem::exists(trans_path)) {
                    path = trans_path;
                } else {
                    CROW_LOG_WARNING << "Streaming failed: Requested format '" << fmt << "' not found for task " << task_id;
                    return utils::error_response("Requested format not available", 404);
                }
            }
            
            // SECURITY: Prevent path traversal and ensure the file is within the storage directory
            if (!utils::is_path_safe(path, manager.get_storage_directory())) {
                CROW_LOG_ERROR << "SECURITY: Blocked path traversal attempt for task " << task_id << ": " << path;
                return utils::error_response("Invalid file path", 403);
            }

            if (!std::filesystem::exists(path)) {
                CROW_LOG_ERROR << "Streaming failed: File not found at " << path;
                return utils::error_response("File not found on disk", 404);
            }

            size_t file_size = std::filesystem::file_size(path);
            crow::response res;
            res.set_header("Accept-Ranges", "bytes");
            
            std::string range_header = req.get_header_value("Range");
            if (range_header.empty()) {
                res.set_static_file_info_unsafe(path);
                return res;
            }

            utils::HttpRange range;
            if (!utils::parse_range_header(range_header, file_size, range)) {
                CROW_LOG_WARNING << "Streaming failed: Invalid Range header: " << range_header;
                return utils::error_response("Invalid Range header", 400);
            }

            if (range.start >= file_size || (range.has_end && range.end >= file_size) || (range.has_end && range.start > range.end)) {
                CROW_LOG_WARNING << "Streaming failed: Range not satisfiable: " << range_header << " (size: " << file_size << ")";
                res.code = 416;
                res.set_header("Content-Range", "bytes */" + std::to_string(file_size));
                return res;
            }

            size_t end = range.has_end ? range.end : (file_size - 1);
            size_t length = end - range.start + 1;
            
            std::ifstream file(path, std::ios::binary);
            file.seekg(range.start);

            std::string buffer(length, '\0');
            file.read(&buffer[0], length);

            res.code = 206;
            res.body = std::move(buffer);
            res.set_header("Content-Range", "bytes " + std::to_string(range.start) + "-" + std::to_string(end) + "/" + std::to_string(file_size));
            res.set_header("Content-Length", std::to_string(length));

            if (!task->content_type.empty()) {
                res.set_header("Content-Type", task->content_type);
            } else {
                std::string ext = std::filesystem::path(path).extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                res.set_header("Content-Type", crow::response::get_mime_type(ext));
            }

            return res;
        });

        // GET /v1/ingested/{task_id} - Get specific ingested file metadata and waveform
        CROW_ROUTE(app, "/v1/ingested/<string>").methods(crow::HTTPMethod::GET)([&manager](std::string task_id) {
            CROW_LOG_INFO << "Metadata request for task: " << task_id;
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
