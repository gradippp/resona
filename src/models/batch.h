#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace models {

struct CreateBatchRequest {
    int wait_duration = 5000;
    int max_retries = 3;
    int max_batch_size = 50;
    std::string max_batch_storage = "5G";
    std::vector<std::string> allowed_services;
    std::string delete_after; // e.g., "24H", "30m"
    int waveform_resolution = 512;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CreateBatchRequest, wait_duration, max_retries, max_batch_size, max_batch_storage, allowed_services, delete_after, waveform_resolution)

struct AddTaskRequest {
    std::string file_id;
    std::string destination_path;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AddTaskRequest, file_id, destination_path)

struct TaskMetadata {
    long long file_size = 0;
    std::string format = "";
    float duration_seconds = 0.0f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TaskMetadata, file_size, format, duration_seconds)

struct Task {
    std::string id;
    std::string file_id;
    std::string destination_path;
    std::string status = "pending"; // pending, downloading, success, failed
    std::string local_url = "";
    std::optional<TaskMetadata> metadata;
    std::vector<float> waveform;
    int waveform_resolution = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Task, id, file_id, destination_path, status, local_url, metadata, waveform, waveform_resolution)

struct Batch {
    std::string id;
    std::string status = "pending"; // pending, processing, completed
    CreateBatchRequest options;
    std::vector<Task> tasks;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Batch, id, status, options, tasks)

} // namespace models
