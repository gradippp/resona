cmake_minimum_required(VERSION 3.18)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(resona 
    VERSION 0.1.1 
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
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)

# MySQL Connector configuration
set(WITH_CURL OFF CACHE BOOL "" FORCE)
set(WITH_SSL ON CACHE BOOL "" FORCE)
set(WITH_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LIBRARY "" CACHE STRING "" FORCE)
set(ZSTD_INCLUDE_DIR "" CACHE STRING "" FORCE)
set(ZSTD_FOUND FALSE CACHE BOOL "" FORCE)
set(CLIENT_PLUGIN_ZSTD "OFF" CACHE STRING "" FORCE)
set(INSTALL_BINDIR "bin" CACHE STRING "" FORCE)
set(INSTALL_LIBDIR "lib" CACHE STRING "" FORCE)
set(INSTALL_PLUGINDIR "lib/plugin" CACHE STRING "" FORCE)
set(INSTALL_INCLUDEDIR "include" CACHE STRING "" FORCE)
set(INSTALL_DOCSDIR "doc" CACHE STRING "" FORCE)
set(INSTALL_MANDIR "man" CACHE STRING "" FORCE)
set(INSTALL_PCDIR "lib/pkgconfig" CACHE STRING "" FORCE)
set(SKIP_INSTALL_ALL ON)

# Fix for bool redefinition in ma_global.h when using newer GCC/Alpine
add_definitions(-DMARIADB_NO_DATATYPES)

# Asio configuration for Crow
set(ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/asio/include" CACHE PATH "" FORCE)
include_directories(${ASIO_INCLUDE_DIR})

# Add dependencies
add_subdirectory(third_party/mariadb-connector-c)

# MariaDB provides 'zlib' target if it uses the bundled one.
# Some dependencies (like curl) expect 'ZLIB::ZLIB'.
if(TARGET zlib AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlib)
endif()

add_subdirectory(third_party/json)
add_subdirectory(third_party/cpr)
add_subdirectory(third_party/crow)
add_subdirectory(third_party/catch2)

# Core library (logic shared between main app and tests)
add_library(resona-core STATIC
    src/routes/routes.cpp
    src/routes/system.cpp
    src/routes/batch.cpp
    src/services/batch_manager.cpp
    src/services/database_service.cpp
    src/services/download_service.cpp
    src/services/media_service.cpp
    src/utils/logger.cpp
    src/utils/size_parser.cpp
)

target_include_directories(resona-core PUBLIC 
    "${PROJECT_BINARY_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mariadb-connector-c/include"
    "${PROJECT_BINARY_DIR}/third_party/mariadb-connector-c/include"
)

target_link_libraries(resona-core PUBLIC 
    Crow 
    nlohmann_json::nlohmann_json 
    cpr
    libmariadb
)

# Main executable
add_executable(resona src/main.cpp)
target_link_libraries(resona PRIVATE resona-core)

# Testing
enable_testing()
add_subdirectory(tests)
