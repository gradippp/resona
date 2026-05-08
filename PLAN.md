# MariaDB Integration for Strata

## Objective
Transition Strata from in-memory state management to a persistent MariaDB database. This ensures that batches and tasks survive service restarts and provides a structured foundation to store complex media data, such as file metadata and extracted waveforms.

## Key Components & Schema
The database will require the following normalized schema:

1. **`batches` Table:**
   - `id` (VARCHAR UUID, Primary Key)
   - `status` (VARCHAR: pending, processing, completed)
   - `options` (JSON: to store wait_duration, max_retries, delete_after, etc.)
   - `created_at` (TIMESTAMP)

2. **`tasks` Table:**
   - `id` (VARCHAR UUID, Primary Key)
   - `batch_id` (VARCHAR UUID, Foreign Key -> batches.id)
   - `file_id` (TEXT: original URL)
   - `destination_path` (TEXT)
   - `status` (VARCHAR: pending, downloading, success, failed)
   - `local_url` (TEXT)
   - `created_at` (TIMESTAMP)

3. **`media_metadata` Table (Future/Current Expansion):**
   - `task_id` (VARCHAR UUID, Primary Key, Foreign Key -> tasks.id)
   - `file_size` (BIGINT)
   - `format` (VARCHAR)
   - `duration_seconds` (FLOAT)

4. **`waveforms` Table:**
   - `task_id` (VARCHAR UUID, Primary Key, Foreign Key -> tasks.id)
   - `waveform_data` (LONGBLOB or JSON: to store arrays of float values representing audio peaks)
   - `resolution` (INT: number of data points)

## Implementation Steps

1. **Add MariaDB Connector Dependency:**
   - Integrate a MariaDB/MySQL C++ Connector (e.g., `mariadb-connector-cpp` or a lightweight alternative) as a Git submodule or via CMake FetchContent, depending on build complexity.
   - Link the connector to `strata-core` in `CMakeLists.txt`.

2. **Create `DatabaseService` (`src/services/database_service.h/cpp`):**
   - Implement a singleton service responsible for managing the connection pool to MariaDB.
   - Add a configuration method that reads DB credentials from environment variables (`DB_HOST`, `DB_USER`, `DB_PASS`, `DB_NAME`).
   - Implement an `initialize_schema()` method to execute `CREATE TABLE IF NOT EXISTS` queries on startup, ensuring the database is ready.

3. **Refactor `BatchManager` (`src/services/batch_manager.cpp`):**
   - **Remove:** The `std::unordered_map<std::string, models::Batch> batches_` and its associated mutex.
   - **`create_batch`:** Execute an `INSERT INTO batches` query.
   - **`add_task`:** Execute an `INSERT INTO tasks` query.
   - **`start_batch`:** Execute `UPDATE batches SET status='processing'` and query all associated tasks to begin downloading. As downloads finish, execute `UPDATE tasks` to set status and local_url.
   - **`get_batch`:** Execute a `SELECT` joining `batches` and `tasks` to reconstruct and return the `models::Batch` object dynamically.

4. **Update Application Startup (`src/main.cpp`):**
   - Initialize `DatabaseService` and verify connectivity before starting the Crow server.

## Migration & Rollback Strategy
- Since this replaces the in-memory map entirely, no data migration is needed for existing (volatile) data.
- The `CREATE TABLE IF NOT EXISTS` statements ensure seamless deployment on fresh MariaDB instances without manual script execution.

## Verification & Testing
- Spin up a local MariaDB instance (e.g., via Docker: `docker run -e MYSQL_ROOT_PASSWORD=root -e MYSQL_DATABASE=strata -p 3306:3306 mariadb`).
- Re-run the existing API Integration Tests (`ctest`). They should pass identically, verifying that the persistent database layer correctly implements the required logic without breaking the API contract.
