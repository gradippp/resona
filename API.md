# Strata API Documentation

This document describes the current REST API endpoints available in the Strata media ingestion and processing service. All endpoints return data in JSON format.

---

## 1. System Endpoints

### 1.1. Get Service Version
Retrieve the current version and description of the service.

**Request:**
`GET /v1/version`

**Response (200 OK):**
```json
{
  "description": "Strata",
  "version": "0.1.0"
}
```

---

## 2. Configuration & Storage

### 2.1. Storage Directory
The service uses the `STORAGE_DIRECTORY` environment variable to determine the root folder for all media files. 
- If `STORAGE_DIRECTORY` is set (e.g., `C:\Data`), then a `destination_path` of `music/song.wav` will be saved to `C:\Data\music\song.wav`.
- Leading slashes in `destination_path` are automatically stripped when resolving against the storage directory.

---

## 3. Batch Ingestion Endpoints

### 3.1. Create a Batch
Initialize a new ingestion batch with specific configuration options.

**Request:**
`POST /v1/batch`

**Body (JSON):**
```json
{
  "wait_duration": 5000,
  "max_retries": 3,
  "max_batch_size": 50,
  "max_batch_storage": "5G",
  "allowed_services": ["GOOGLE_DRIVE", "DROPBOX"],
  "delete_after": "24H"
}
```
*Note: `delete_after` supports values like `24H` (hours), `30m` (minutes), or `60s` (seconds). If provided, the source files will be automatically deleted after the specified duration once the batch is completed.*

**Response (200 OK):**
```json
{
  "batch_id": "uuid-v4-string",
  "status": "pending"
}
```

### 3.2. Add an Ingestion Task to a Batch
Queue a specific media ingestion task.

**Request:**
`POST /v1/batch/{batch_id}`

**Body (JSON):**
```json
{
  "file_id": "https://www.dropbox.com/...&dl=0",
  "destination_path": "my_file.wav"
}
```
*Note: Dropbox URLs with `dl=0` are automatically converted to `dl=1` for direct downloading.*

---

## 4. Licenses
This project is licensed under the **DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE (WTFPL)**. See the `LICENSE` file for details.
