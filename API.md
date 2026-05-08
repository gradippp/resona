# Resona API Documentation

This document describes the current REST API endpoints available in the Resona media ingestion and processing service. All endpoints return data in JSON format.

---

## 1. System Endpoints

### 1.1. Get Service Version
Retrieve the current version and description of the service.

**Request:**
`GET /v1/version`

**Response (200 OK):**
```json
{
  "description": "Resona",
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
  "allowed_services": ["GOOGLE_DRIVE", "DROPBOX", "DIRECT"],
  "delete_after": "24H",
  "waveform_resolution": 256
}
```
*Note: `delete_after` is **required** and supports values like `24H` (hours), `30m` (minutes), or `60s` (seconds). Source files will be automatically deleted after the specified duration once the batch is completed. `waveform_resolution` specifies the number of peak data points to extract (default is 256).*

**Response (200 OK):**
```json
{
  "batch_id": "uuid-v4-string",
  "status": "pending"
}
```

### 3.2. Add an Ingestion Task to a Batch
Queue a specific media ingestion task. This can be called while a batch is `pending` or `awaiting`.

**Request:**
`POST /v1/batch/{batch_id}`

**Body (JSON):**
```json
{
  "file_id": "https://www.dropbox.com/...&dl=0",
  "destination_path": "my_file.wav"
}
```
*Note: Resona automatically handles direct download links for Dropbox and Google Drive. Supported `allowed_services` types are `DROPBOX`, `GOOGLE_DRIVE`, and `DIRECT` (for standard URLs).*

**Response (202 Accepted):**
```json
{
  "message": "Task added to batch"
}
```

### 3.3. Start a Batch
Begin processing tasks for a batch. The batch will transition to `awaiting` status.

**Request:**
`POST /v1/batch/{batch_id}/start`

**Response (200 OK):**
```json
{
  "batch_id": "uuid-v4-string",
  "status": "awaiting"
}
```
*Note: In the `awaiting` state, the background monitor will automatically process any tasks added to the batch after the configured `wait_duration` has elapsed. The batch remains in this state until manually completed.*

### 3.4. Check Batch Status & Results
Retrieve the current status of a batch and the results (metadata/waveforms) of its tasks.

**Request:**
`GET /v1/batch/{batch_id}`

**Response (200 OK):**
```json
{
  "id": "uuid-v4-string",
  "status": "awaiting",
  "options": {
    "wait_duration": 5000,
    "max_retries": 3,
    "max_batch_size": 50,
    "max_batch_storage": "5G",
    "allowed_services": ["GOOGLE_DRIVE"],
    "delete_after": "24H",
    "waveform_resolution": 256
  },
  "tasks": [
    {
      "id": "task-uuid",
      "file_id": "https://...",
      "destination_path": "C:\\Data\\file.wav",
      "status": "success",
      "local_url": "file://C:/Data/file.wav",
      "metadata": {
        "file_size": 1048576,
        "format": "WAV",
        "duration_seconds": 120.5
      },
      "waveform": [0.1, 0.45, 0.9, 0.2, "..."]
    }
  ]
}
```

### 3.5. Mark a Batch as Complete
Finalize a batch and mark it as completed. No more tasks can be added, and the cleanup timer for `delete_after` will start.

**Request:**
`POST /v1/batch/{batch_id}/complete`

**Response (200 OK):**
```json
{
  "batch_id": "uuid-v4-string",
  "status": "completed"
}
```

### 3.6. List All Ingested Data
Retrieve metadata for all successfully ingested files across all batches.

**Request:**
`GET /v1/ingested`

**Response (200 OK):**
```json
[
  {
    "id": "task-uuid",
    "file_id": "https://...",
    "destination_path": "C:\\Data\\file.wav",
    "status": "success",
    "local_url": "file://C:/Data/file.wav",
    "metadata": {
      "file_size": 1048576,
      "format": "WAV",
      "duration_seconds": 120.5
    }
  }
]
```

---

## 4. Licenses
This project is licensed under the **DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE (WTFPL)**. See the `LICENSE` file for details.
