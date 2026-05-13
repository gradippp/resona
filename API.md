# Resona API Documentation

This document describes the REST API endpoints available in the Resona media ingestion and processing service. All endpoints return data in JSON format.

---

## 1. Global Standards

### 1.1. Error Response Schema
All non-200 series responses follow the **RFC 7807 (Problem Details for HTTP APIs)** standard:

```json
{
  "type": "about:blank",
  "title": "Bad Request",
  "status": 400,
  "detail": "Short description of the error"
}
```

### 1.2. Status Codes
- `200 OK`: Request succeeded.
- `202 Accepted`: Task/Batch successfully queued or updated.
- `400 Bad Request`: Validation failed (e.g., missing required fields, batch full).
- `404 Not Found`: The requested Batch or Task UUID does not exist.

**Note on Batch Statuses:**
Batches transition through several states: `pending` $\rightarrow$ `awaiting` $\rightarrow$ `completed` $\rightarrow$ `deleted`.
- `awaiting`: The batch is active and processing tasks.
- `completed`: The batch is finalized, downloads are finished, and the `delete_after` timer is active.
- `deleted`: The batch's local files and directory have been permanently removed by the cleanup worker.

**Note on Waveform Extraction:**
Resona uses a professional **streaming, min/max peak-envelope extraction** pipeline.
- **Low Memory:** Audio is processed in chunks; even hour-long files consume only a few kilobytes of RAM during extraction.
- **Visual Fidelity:** Unlike RMS-based systems, this pipeline preserves transients (drum hits/drops) and applies a perceptual gamma curve to enhance visibility of quiet details.
- **Symmetric Data:** Each window contains both a minimum and maximum peak, allowing for professional, symmetric waveform rendering.

**Note on Task Statuses:** 
Tasks transition through several states: `pending` $\rightarrow$ `processing` $\rightarrow$ `downloading` $\rightarrow$ `success` or `failed`. 
- `processing`: The task is in the active batch queue and waiting for its turn in the sequential processing loop.

---

## 2. System Endpoints

### 2.1. Get Service Version
Retrieve the current version and description of the service.

**Request:**
`GET /v1/version`

**Response (200 OK):**
```json
{
  "description": "Resona",
  "version": "0.7.0"
}
```

---

## 3. Configuration & Storage

### 3.1. Storage Directory
The service uses the `STORAGE_DIRECTORY` environment variable to determine the root folder for all media files. 
- Files are automatically saved using a structured hierarchy: `${STORAGE_DIRECTORY}/{batch_id}/{task_id}.{ext}`.
- The extension `{ext}` is derived automatically from the source URL.
- Example: `STORAGE_DIRECTORY=C:\Data`, `batch_id=b1`, `task_id=t1`, `url=.../song.wav` $\rightarrow$ `C:\Data\b1\t1.wav`.

---

## 4. Batch Ingestion Endpoints

### 4.1. Create a Batch
Initialize a new ingestion batch.

**Request:**
`POST /v1/batch`

**Body (JSON):**
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `delete_after` | string | **Required** | Duration to keep files after completion (e.g., "24H", "30m", "60s"). |
| `wait_duration` | string | "5s" | Acts as both an initial buffer before processing begins and a cooldown delay between consecutive downloads in the batch. |
| `max_retries` | int | 3 | Number of times to retry a failed download. |
| `max_batch_size` | int | 50 | Maximum number of tasks allowed in this batch. |
| `max_batch_storage` | string | "5G" | Total byte limit for the batch (e.g., "1G", "500M"). |
| `allowed_services` | array | [] | List of permitted providers: `["GOOGLE_DRIVE", "DROPBOX", "DIRECT"]`. Empty = All allowed. |
| `allowed_content_types` | array | [] | List of permitted MIME types (e.g., `["audio/mpeg", "audio/wav"]`). Empty = All allowed. |
| `waveform_resolution`| int | 4096 | Number of window pairs (min/max) to extract per audio file. |

**Response (200 OK):**
```json
{
  "batch_id": "uuid-v4-string",
  "status": "pending"
}
```

### 4.2. Add a Task to a Batch
Queue a specific media ingestion task.

**Request:**
`POST /v1/batch/{batch_id}`

**Body (JSON):**
| Field | Type | Description |
| :--- | :--- | :--- |
| `file_id` | string | The source URL (Dropbox, Google Drive, or direct link). |

**Response (202 Accepted):**
```json
{
  "message": "Task queued in batch <batch_id>"
}
```

**Error (400 Bad Request):**
```json
{
  "type": "about:blank",
  "title": "Bad Request",
  "status": 400,
  "detail": "Batch <uuid> has reached max size of 50"
}
```
or 
```json
{
  "type": "about:blank",
  "title": "Bad Request",
  "status": 400,
  "detail": "Service type 'DIRECT' not allowed for URL: ..."
}
```

**Error (404 Not Found):**
```json
{
  "type": "about:blank",
  "title": "Not Found",
  "status": 404,
  "detail": "Batch not found or already started"
}
```

### 4.3. Start a Batch
Transitions a `pending` batch to the `awaiting` state to begin the background monitor loop.

**Request:**
`POST /v1/batch/{batch_id}/start`

**Response (200 OK):**
```json
{
  "message": "Batch <batch_id> started"
}
```

**Error (404 Not Found):**
```json
{
  "type": "about:blank",
  "title": "Not Found",
  "status": 404,
  "detail": "Batch not found or already started"
}
```

### 4.4. Check Batch Status & Results
Retrieve the status and results of tasks in a batch.

**Request:**
`GET /v1/batch/{batch_id}`

**Response (200 OK):**
```json
{
  "id": "uuid-v4",
  "status": "awaiting",
  "options": { ... },
  "tasks": [
    {
      "id": "task-uuid",
      "status": "success"
    }
  ]
}
```

---

### 4.5. Mark a Batch as Complete
Finalizes a batch. No more tasks can be added, and the `delete_after` cleanup timer begins.

**Request:**
`POST /v1/batch/{batch_id}/complete`

**Response (200 OK):**
`{"batch_id": "uuid", "status": "completed"}`

**Error (404 Not Found):**
```json
{
  "type": "about:blank",
  "title": "Not Found",
  "status": 404,
  "detail": "Batch not found or not in awaiting state"
}
```

---

## 5. Data Access Endpoints

### 5.1. Get Specific Ingested Data
Direct lookup of a successfully ingested file by its unique Task UUID.

**Request:**
`GET /v1/ingested/{task_id}`

**Response (200 OK):**
```json
{
  "id": "task-uuid",
  "file_id": "https://...",
  "status": "success",
  "local_url": "file:///...",
  "metadata": {
    "file_size": 1048576,
    "format": "WAV",
    "duration_seconds": 120.5
  },
  "waveform_peaks_b64": "base64-encoded-binary-string",
  "waveform_resolution": 4096
}
```

#### Waveform Format Details
1. **`waveform_peaks_b64`:** A base64-encoded binary string. When decoded, it is an array of `int16_t` pairs: `[min0, max0, min1, max1, ...]`.
   - **Quantization:** Float peaks $[-1.0, 1.0]$ are multiplied by $32767$ to store as `int16`.
   - **Consumption:** Decode base64 to a `Uint8Array`, then interpret as an `Int16Array`.
   - **Advantage:** Vastly more network-efficient than JSON arrays and allows for symmetric, professional rendering.

### 5.2. Stream Ingested File
Stream a successfully ingested media file. This endpoint is optimized for audio streaming and supports seeking via standard HTTP Range requests, making it compatible with HTML5 `<audio>` and `<video>` tags.

**Request:**
`GET /v1/ingested/{task_id}/stream`

**Headers:**
- `Range`: (Optional) Standard HTTP range header (e.g., `bytes=0-1024` or `bytes=2048-`).

**Response:**
- `200 OK`: Returns the full file content (standard download/stream).
- `206 Partial Content`: Returns the requested byte range. Essential for seeking in audio/video players.
- `400 Bad Request`: If the task hasn't finished downloading yet or if the `Range` header is invalid.
- `403 Forbidden`: If the file path is invalid or a path traversal attempt is detected.
- `404 Not Found`: If the Task UUID doesn't exist or the file has been deleted from disk.
- `416 Range Not Satisfiable`: If the requested byte range is outside the file's actual size.

**Headers in Response:**
- `Accept-Ranges: bytes`: Informs the client that seeking is supported.
- `Content-Range`: (On 206) Specifies the range and total size (e.g., `bytes 0-1024/5000000`).
- `Content-Type`: Automatically set based on the file extension (e.g., `audio/mpeg`, `audio/wav`).
- `Content-Length`: The size of the content being returned in the current response.

---

## 6. Licenses
This project is licensed under the **MIT License**.
