# Cloud Download Service API Documentation

This document describes the current REST API endpoints available in the Cloud Download Service. All endpoints return data in JSON format.

---

## 1. System Endpoints

### 1.1. Get Service Version
Retrieve the current version and description of the service.

**Request:**
`GET /v1/version`

**Response (200 OK):**
```json
{
  "description": "Cloud Download Service",
  "version": "0.1.0"
}
```

---

## 2. Batch Management Endpoints

### 2.1. Create a Batch
Initialize a new download batch with specific configuration options.

**Request:**
`POST /v1/batch`

**Body (JSON):**
```json
{
  "wait_duration": 5000,
  "max_retries": 3,
  "max_batch_size": 50,
  "max_batch_storage": "5G",
  "allowed_services": ["GOOGLE_DRIVE", "DROPBOX"]
}
```
*Note: All fields in the body are optional and will default to standard values if omitted.*

**Response (200 OK):**
```json
{
  "batch_id": "8b3f2a1e-5c90-4d7a-b21f-e8a9c3d4f5g6",
  "status": "pending"
}
```

### 2.2. Add a Task to a Batch
Queue a specific file download task within an existing, pending batch.

**Request:**
`POST /v1/batch/{batch_id}`

**Body (JSON):**
```json
{
  "file_id": "file-identifier-string",
  "destination_path": "/path/to/save/file.ext"
}
```

**Response (202 Accepted):**
```json
{
  "message": "Task queued in batch {batch_id}"
}
```
**Errors:**
- `404 Not Found`: Returned if the batch does not exist or has already been started.

### 2.3. Start a Batch
Initiate the processing (downloading) for all queued tasks in a specific batch.

**Request:**
`POST /v1/batch/{batch_id}/start`

**Response (200 OK):**
```json
{
  "message": "Batch {batch_id} started"
}
```
**Errors:**
- `404 Not Found`: Returned if the batch does not exist or is already processing/completed.

### 2.4. Get Batch Status
Retrieve the current status and options of a batch, including the status of all its individual tasks.

**Request:**
`GET /v1/batch/{batch_id}`

**Response (200 OK):**
```json
{
  "id": "8b3f2a1e-5c90-4d7a-b21f-e8a9c3d4f5g6",
  "status": "completed",
  "options": {
    "allowed_services": ["GOOGLE_DRIVE", "DROPBOX"],
    "max_batch_size": 50,
    "max_batch_storage": "5G",
    "max_retries": 3,
    "wait_duration": 5000
  },
  "tasks": [
    {
      "id": "c1a2b3c4-d5e6-4f7a-8b9c-0d1e2f3a4b5c",
      "file_id": "file-identifier-string",
      "destination_path": "/path/to/save/file.ext",
      "status": "success",
      "local_url": "/downloads/c1a2b3c4-d5e6-4f7a-8b9c-0d1e2f3a4b5c.zip"
    }
  ]
}
```
*Note: Task statuses can be `pending`, `downloading`, `success`, or `failed`.*

**Errors:**
- `404 Not Found`: Returned if the requested batch does not exist.
