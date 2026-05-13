#pragma once
#include <mysql.h>
#include <string>
#include <cstring>
#include "crow/logging.h"

namespace utils {

/**
 * RAII Wrapper for MYSQL_STMT to ensure statements are always closed.
 * Also provides helper methods to reduce boilerplate.
 */
class StatementWrapper {
public:
    explicit StatementWrapper(MYSQL* conn) : stmt_(nullptr) {
        if (conn) {
            stmt_ = mysql_stmt_init(conn);
            if (!stmt_) {
                CROW_LOG_ERROR << "mysql_stmt_init() failed";
            }
        }
    }

    ~StatementWrapper() {
        if (stmt_) {
            mysql_stmt_close(stmt_);
        }
    }

    // Disable copy
    StatementWrapper(const StatementWrapper&) = delete;
    StatementWrapper& operator=(const StatementWrapper&) = delete;

    // Allow move
    StatementWrapper(StatementWrapper&& other) noexcept : stmt_(other.stmt_) {
        other.stmt_ = nullptr;
    }
    StatementWrapper& operator=(StatementWrapper&& other) noexcept {
        if (this != &other) {
            if (stmt_) mysql_stmt_close(stmt_);
            stmt_ = other.stmt_;
            other.stmt_ = nullptr;
        }
        return *this;
    }

    operator MYSQL_STMT*() { return stmt_; }
    bool isValid() const { return stmt_ != nullptr; }

    bool prepare(const std::string& query) {
        if (!stmt_) return false;
        if (mysql_stmt_prepare(stmt_, query.c_str(), query.length())) {
            unsigned int err = mysql_stmt_errno(stmt_);
            const char* msg = mysql_stmt_error(stmt_);
            CROW_LOG_ERROR << "mysql_stmt_prepare() failed. Error " << err << ": " << msg << " (Query: " << query << ")";
            return false;
        }
        return true;
    }

    bool bind_params(MYSQL_BIND* bind) {
        if (!stmt_) return false;
        if (mysql_stmt_bind_param(stmt_, bind)) {
            unsigned int err = mysql_stmt_errno(stmt_);
            const char* msg = mysql_stmt_error(stmt_);
            CROW_LOG_ERROR << "mysql_stmt_bind_param() failed. Error " << err << ": " << msg;
            return false;
        }
        return true;
    }

    bool execute() {
        if (!stmt_) return false;
        if (mysql_stmt_execute(stmt_)) {
            unsigned int err = mysql_stmt_errno(stmt_);
            const char* msg = mysql_stmt_error(stmt_);
            CROW_LOG_ERROR << "mysql_stmt_execute() failed. Error " << err << ": " << msg;
            return false;
        }
        return true;
    }

    bool bind_result(MYSQL_BIND* bind) {
        if (!stmt_) return false;
        if (mysql_stmt_bind_result(stmt_, bind)) {
            unsigned int err = mysql_stmt_errno(stmt_);
            const char* msg = mysql_stmt_error(stmt_);
            CROW_LOG_ERROR << "mysql_stmt_bind_result() failed. Error " << err << ": " << msg;
            return false;
        }
        return true;
    }

    bool store_result() {
        if (!stmt_) return false;
        if (mysql_stmt_store_result(stmt_)) {
            unsigned int err = mysql_stmt_errno(stmt_);
            const char* msg = mysql_stmt_error(stmt_);
            CROW_LOG_ERROR << "mysql_stmt_store_result() failed. Error " << err << ": " << msg;
            return false;
        }
        return true;
    }

    bool reset() {
        if (!stmt_) return false;
        if (mysql_stmt_reset(stmt_)) {
            CROW_LOG_ERROR << "mysql_stmt_reset() failed: " << mysql_stmt_error(stmt_);
            return false;
        }
        return true;
    }

    int fetch() {
        if (!stmt_) return MYSQL_NO_DATA;
        return mysql_stmt_fetch(stmt_);
    }

    unsigned long long num_rows() {
        if (!stmt_) return 0;
        return mysql_stmt_num_rows(stmt_);
    }

    unsigned long long affected_rows() {
        if (!stmt_) return 0;
        return mysql_stmt_affected_rows(stmt_);
    }

    // New ergonomic helpers
    bool bind_string_param(int index, const std::string& val, MYSQL_BIND* binds) {
        if (index < 0) return false;
        binds[index].buffer_type = MYSQL_TYPE_STRING;
        binds[index].buffer = (void*)val.c_str();
        binds[index].buffer_length = (unsigned long)val.length();
        binds[index].is_null = nullptr;
        binds[index].length = nullptr;
        return true;
    }

    bool bind_int_param(int index, int* val, MYSQL_BIND* binds) {
        if (index < 0) return false;
        binds[index].buffer_type = MYSQL_TYPE_LONG;
        binds[index].buffer = (void*)val;
        binds[index].is_null = nullptr;
        binds[index].length = nullptr;
        return true;
    }

    bool bind_long_param(int index, long long* val, MYSQL_BIND* binds) {
        if (index < 0) return false;
        binds[index].buffer_type = MYSQL_TYPE_LONGLONG;
        binds[index].buffer = (void*)val;
        binds[index].is_null = nullptr;
        binds[index].length = nullptr;
        return true;
    }

    bool bind_float_param(int index, float* val, MYSQL_BIND* binds) {
        if (index < 0) return false;
        binds[index].buffer_type = MYSQL_TYPE_FLOAT;
        binds[index].buffer = (void*)val;
        binds[index].is_null = nullptr;
        binds[index].length = nullptr;
        return true;
    }

    bool bind_double_param(int index, double* val, MYSQL_BIND* binds) {
        if (index < 0) return false;
        binds[index].buffer_type = MYSQL_TYPE_DOUBLE;
        binds[index].buffer = (void*)val;
        binds[index].is_null = nullptr;
        binds[index].length = nullptr;
        return true;
    }

    bool bind_blob_param(int index, const void* data, unsigned long length, MYSQL_BIND* binds) {
        if (index < 0) return false;
        binds[index].buffer_type = MYSQL_TYPE_BLOB;
        binds[index].buffer = (void*)data;
        binds[index].buffer_length = length;
        binds[index].is_null = nullptr;
        binds[index].length = nullptr;
        return true;
    }

    bool bind_null_param(int index, MYSQL_BIND* binds) {
        if (index < 0) return false;
        binds[index].buffer_type = MYSQL_TYPE_NULL;
        static char is_null = 1;
        binds[index].is_null = &is_null;
        return true;
    }

    const char* last_error() const {
        return stmt_ ? mysql_stmt_error(stmt_) : "Invalid Statement";
    }

    unsigned int last_errno() const {
        return stmt_ ? mysql_stmt_errno(stmt_) : 0;
    }

private:
    MYSQL_STMT* stmt_;
};

} // namespace utils
