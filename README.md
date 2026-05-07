# Cloud Download Service (cloud-dl-service)

A focused microservice designed for a single purpose: downloading files in batches from cloud storage providers. It currently supports direct HTTP downloads and Dropbox integration.

## Features
- **Batch Processing**: Group multiple file downloads into manageable batches.
- **Asynchronous Execution**: Downloads happen in background threads, keeping the API responsive.
- **Auto-Cleanup**: Automatically delete downloaded files after a specified duration.
- **Dropbox Support**: Direct downloading from Dropbox links (automatic `dl=1` conversion).

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
    cd cloud-dl-service
    git submodule update --init --recursive --depth 1
    ```

2.  **Configure and Build:**
    ```powershell
    cmake -B build -G "Visual Studio 18 2026" -A x64 -DCPR_CURL_USE_LIBPSL=OFF
    cmake --build build --config Release --parallel 8
    ```

### Configuration
The service is configured via environment variables:
- `STORAGE_DIRECTORY`: (Required) The root directory where all files will be saved.
- `PORT`: (Default: 8080) The port the REST API listens on.

## Running the Service
Ensure you have set the `STORAGE_DIRECTORY` environment variable before starting:
```powershell
$env:STORAGE_DIRECTORY = "C:\path\to\downloads"
.\build\Release\cloud-dl-service.exe
```

## API Documentation
For detailed information on available endpoints, request formats, and responses, please refer to:
👉 **[API.md](./API.md)**

## License
This project is licensed under the **DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE (WTFPL)**.
