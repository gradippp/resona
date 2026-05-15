# Resona

A focused microservice designed for media ingestion and waveform processing. It handles batch processing of media files, performing downloads from various cloud providers and extracting analytical data like waveforms.

## Features
- **Batch Processing**: Group multiple file ingestion tasks into manageable batches.
- **Asynchronous Execution**: Ingestion and processing happen in background threads, keeping the API responsive.
- **Native Media Transcoding**: High-performance transcoding to formats like **MP3** and **OGG/Vorbis** powered by FFmpeg.
- **Auto-Cleanup**: Automatically delete processed source files after a specified duration.
- **Cloud Support**: Supports Dropbox and Google Drive.
- **Professional Waveforms**: Constant-memory, streaming extraction of symmetric min/max peak-envelopes. Optimized for transient-rich audio with perceptual gamma scaling.
- **Low-Latency Streaming**: Optimized media delivery with support for HTTP Range seeking and on-the-fly format selection.

## Setup & Installation

### Prerequisites
- **CMake** (3.18 or higher)
- **C++ Compiler** (C++17 support required, e.g., Visual Studio 2022/2026, GCC, or Clang)
- **Git**
- **FFmpeg** (v5.1 or higher): Required for media transcoding and analytics.
- **MariaDB / MySQL**: An active database instance is required for the application and tests.

### System Dependencies (FFmpeg)

#### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y libavformat-dev libavcodec-dev libavutil-dev libswresample-dev pkg-config
```

#### macOS (Homebrew)
```bash
brew install ffmpeg pkg-config
```

#### Alpine Linux (Docker)
```bash
apk add ffmpeg-dev ffmpeg-libs pkgconf
```

### Building the Project
The project uses git submodules for dependencies.

1.  **Clone and Fetch Dependencies:**
    ```powershell
    git clone <repository-url>
    cd resona
    git submodule update --init --recursive --depth 1
    ```

2.  **Configure and Build:**
    
    **Windows (vcpkg):**
    ```powershell
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
    cmake --build build --config Release
    ```

    **Linux / macOS:**
    ```bash
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j$(nproc)
    ```

### Configuration
The service is configured via environment variables:
- `STORAGE_DIRECTORY`: The root directory for media storage. Defaults to `data` if not set. Files are automatically structured as `${STORAGE_DIRECTORY}/${BATCH_ID}/${TASK_ID}.${ext}`.
- `HOSTNAME`: (Default: 0.0.0.0) The hostname or IP address the REST API binds to.
- `PORT`: (Default: 8080) The port the REST API listens on.
- `DATABASE_URL`: (Required) Database connection URL in the format `mysql://[user[:pass]@]host[:port]/dbname`.
  Example: `mysql://resona_user:resona_password@localhost:3306/resona`

## Running the Service

### Database Setup
Before running, ensure a MariaDB instance is available. If using the provided `docker-compose.yml`:
```powershell
docker-compose up -d mariadb
```

### Starting the Server
Ensure you have set the `STORAGE_DIRECTORY` environment variable before starting:
```powershell
$env:STORAGE_DIRECTORY = "C:\path\to\data"
.\build\Release\resona.exe
```

## Running Tests
To run the test suite, the MariaDB service must be running and exposing port 3306 (or configured via environment variables):
```powershell
cd build
ctest --output-on-failure -C Release
```

## API Documentation
For detailed information on available endpoints, request formats, and responses, please refer to:
👉 **[API.md](./API.md)**

## Security & Architecture Assumptions
Resona is designed to operate as a specialized worker microservice within a larger, managed infrastructure. Consequently, the following security and operational concerns are assumed to be handled by an upstream orchestrator or API gateway:
- **SSRF Mitigation**: The service performs fetches for any provided URL. Upstream units must validate and sanitize URLs (e.g., resolving hostnames and checking against private/local IP ranges) before passing them to Resona to prevent Server-Side Request Forgery.
- **Global Rate Limiting**: There are no internal global rate limits across all users. The orchestrating service is responsible for implementing rate limits to prevent overall resource exhaustion.

### Built-in Constraints
While global limits are deferred, Resona enforces the following per-batch constraints to protect the immediate environment:
- **Batch Size Limit**: Each batch is limited to a maximum number of files (default: 50).
- **Storage Quota**: Batches enforce a total cumulative file size limit (default: 5GB) during the download phase to prevent runaway storage consumption.

## License
This project is licensed under the **MIT License**. See the [LICENSE](./LICENSE) file for details.
