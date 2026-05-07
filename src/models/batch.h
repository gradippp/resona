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
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CreateBatchRequest, wait_duration, max_retries, max_batch_size, max_batch_storage, allowed_services)

} // namespace models
