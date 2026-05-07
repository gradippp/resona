cmake_minimum_required(VERSION 3.18)
project(strata 
    VERSION 0.1.0 
    DESCRIPTION "Microservice for media ingestion and waveform processing"
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
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)

# Asio configuration for Crow
set(ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/include" CACHE PATH "" FORCE)
include_directories(${ASIO_INCLUDE_DIR})

# Add dependencies
add_subdirectory(third_party/crow)
add_subdirectory(third_party/json)
add_subdirectory(third_party/cpr)
add_subdirectory(third_party/catch2)

# Core library (logic shared between main app and tests)
add_library(strata-core STATIC
    src/routes/routes.cpp
    src/routes/system.cpp
    src/routes/batch.cpp
    src/services/batch_manager.cpp
    src/services/download_service.cpp
    src/utils/logger.cpp
)

target_include_directories(strata-core PUBLIC 
    "${PROJECT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(strata-core PUBLIC 
    Crow 
    nlohmann_json::nlohmann_json 
    cpr
)

# Main executable
add_executable(strata src/main.cpp)
target_link_libraries(strata PRIVATE strata-core)

# Testing
enable_testing()
add_subdirectory(tests)
