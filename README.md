# Resona

A focused microservice designed for media ingestion and waveform processing. It handles batch processing of media files, performing downloads from various cloud providers and extracting analytical data like waveforms.

## Features
- **Batch Processing**: Group multiple file ingestion tasks into manageable batches.
- **Asynchronous Execution**: Ingestion and processing happen in background threads, keeping the API responsive.
- **Auto-Cleanup**: Automatically delete processed source files after a specified duration.
- **Cloud Support**: Supports Dropbox and Google Drive.
- **Waveform Extraction**: Extraction of audio waveform data for visualization and analysis.

## Setup & Installation

### Prerequisites
- **CMake** (3.18 or higher)
- **C++ Compiler** (C++17 support required, e.g., Visual Studio 2022/2026, GCC, or Clang)
- **Git**
- **MariaDB / MySQL**: An active database instance is required for the application and tests.

### Building the Project
The project uses git submodules for dependencies.

1.  **Clone and Fetch Dependencies:**
    ```powershell
    git clone <repository-url>
    cd resona
    git submodule update --init --recursive --depth 1
    ```

2.  **Configure and Build:**
    ```powershell
    cmake -B build -G "Visual Studio 18 2026" -A x64 -DCPR_CURL_USE_LIBPSL=OFF
    cmake --build build --config Release --parallel 8
    ```

### Configuration
The service is configured via environment variables:
- `STORAGE_DIRECTORY`: The root directory for media storage. Defaults to `data` if not set. Files are automatically structured as `${STORAGE_DIRECTORY}/${BATCH_ID}/${TASK_ID}.${ext}`.
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

## License
This project is licensed under the **MIT License**. See the [LICENSE](./LICENSE) file for details.
