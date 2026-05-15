#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace nlohmann {
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void to_json(json& j, const std::optional<T>& opt) {
            if (opt.has_value()) {
                j = *opt;
            } else {
                j = nullptr;
            }
        }

        static void from_json(const json& j, std::optional<T>& opt) {
            if (j.is_null()) {
                opt = std::nullopt;
            } else {
                opt = j.get<T>();
            }
        }
    };
}

namespace models {

struct CreateBatchRequest {
    std::string wait_duration = "5s";
    int max_retries = 3;
    int max_batch_size = 50;
    std::string max_batch_storage = "5G";
    std::vector<std::string> allowed_services;
    std::vector<std::string> allowed_content_types;
    std::string delete_after; // e.g., "24H", "30m"
    int waveform_resolution = 4096;
    std::vector<std::string> transcode_formats;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CreateBatchRequest, wait_duration, max_retries, max_batch_size, max_batch_storage, allowed_services, allowed_content_types, delete_after, waveform_resolution, transcode_formats)

struct AddTaskRequest {
    std::string file_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AddTaskRequest, file_id)

struct TaskMetadata {
    long long file_size = 0;
    std::string format = "";
    float duration_seconds = 0.0f;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TaskMetadata, file_size, format, duration_seconds)

struct TaskSummary {
    std::string id;
    std::string status;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TaskSummary, id, status)

struct MediaUrl {
    std::string url;
    std::string content_type;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaUrl, url, content_type)

struct Task {
    std::string id;
    std::string file_id;
    std::string content_type = "";
    std::string destination_path;
    std::string status = "pending"; // pending, processing, downloading, success, failed
    std::vector<MediaUrl> local_urls;
    std::vector<MediaUrl> stream_urls;
    std::optional<TaskMetadata> metadata;
    std::string waveform_peaks_b64 = "";
    int waveform_resolution = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Task, id, file_id, status, local_urls, stream_urls, metadata, waveform_peaks_b64, waveform_resolution)

struct WaveformPointInt16 {
    int16_t minPeak;
    int16_t maxPeak;
};

struct Batch {
    std::string id;
    std::string status = "pending"; // pending, processing, completed
    CreateBatchRequest options;
    std::vector<TaskSummary> tasks;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Batch, id, status, options, tasks)

} // namespace models
