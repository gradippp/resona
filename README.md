# Cloud Download Service (cloud-dl-service)

A focused microservice designed for a single purpose: downloading files in batches from cloud storage providers like Google Drive and Dropbox.

## Features
- **Batch Processing**: Group multiple file downloads into manageable batches.
- **Multi-Cloud Support**: Fetch files seamlessly from Google Drive and Dropbox.
- **Simple REST API**: Easy integration with your main application backend.

## API Reference

### Create a Batch
Initialize a new download batch.

**`POST /v1/batch`**
- **Request Body (`create_options`):**
  ```json
  {
    "wait_duration": 5000,
    "max_retries": 3,
    "max_batch_size": 50,
    "max_batch_storage": "5G",
    "allowed_services": ["GOOGLE_DRIVE", "DROPBOX"]
  }
  ```
- **Response:**
  ```json
  {
    "batch_id": "uuid-v4-string",
    "status": "pending"
  }
  ```

### Add a Download Task to a Batch
Queue a specific file for download within an existing batch.

**`POST /v1/batch/{batch_id}`**
- **Body:** File details (e.g., file ID, destination path)

### Start a Batch
Initiate the download process for all tasks in a batch.

**`POST /v1/batch/{batch_id}/start`**

### Check Batch Status
Retrieve the current progress and status of a batch.

**`GET /v1/batch/{batch_id}`**
- **Response:**
  ```json
  {
    "batch_id": "uuid-v4-string",
    "status": "completed",
    "tasks": [
      {
        "file_id": "file-1",
        "status": "success",
        "local_url": "/a1b2c3d4-e5f6-g7h8-i9j0-k1l2m3n4o5p6.zip"
      }
    ]
  }
  ```

## Setup & Configuration
*(To be completed based on specific implementation details)*

- Environment variables: (e.g., `PORT`)
