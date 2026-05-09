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
- `STORAGE_DIRECTORY`: (Required) The root directory where all files will be saved and processed.
- `PORT`: (Default: 8080) The port the REST API listens on.

## Running the Service
Ensure you have set the `STORAGE_DIRECTORY` environment variable before starting:
```powershell
$env:STORAGE_DIRECTORY = "C:\path\to\data"
.\build\Release\resona.exe
```

## API Documentation
For detailed information on available endpoints, request formats, and responses, please refer to:
👉 **[API.md](./API.md)**

## License
This project is licensed under the **MIT License**. See the [LICENSE](./LICENSE) file for details.
