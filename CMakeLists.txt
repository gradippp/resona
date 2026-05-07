cmake_minimum_required(VERSION 3.18)
project(cloud-dl-service 
    VERSION 0.1.0 
    DESCRIPTION "Microservice for batch cloud file downloads"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Version header generation
configure_file(
    "${PROJECT_SOURCE_DIR}/src/version.h.in"
    "${PROJECT_BINARY_DIR}/generated/version.h"
)

# Submodule configuration
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

# Asio configuration for Crow
set(ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/include" CACHE PATH "" FORCE)
include_directories(${ASIO_INCLUDE_DIR})

# Add dependencies
add_subdirectory(third_party/crow)
add_subdirectory(third_party/json)
add_subdirectory(third_party/cpr)

# Main executable
add_executable(cloud-dl-service src/main.cpp)
target_include_directories(cloud-dl-service PRIVATE "${PROJECT_BINARY_DIR}/generated")

# Link libraries
target_link_libraries(cloud-dl-service PRIVATE Crow nlohmann_json::nlohmann_json cpr)

# Include directories (if needed beyond what the targets provide, though usually they handle it)
# Crow target is INTERFACE, nlohmann_json is also INTERFACE, cpr is a library.
