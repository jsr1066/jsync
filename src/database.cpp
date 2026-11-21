#include "database.h"

#include <filesystem>
#include <system_error>

#include <cerrno>

#include <sqlite3.h>

namespace jsync_ns
{
    namespace
    {
        // Schema from jsync-design/database.md. create() runs it in a
        // transaction so a failure leaves no tables behind.
        constexpr const char* SCHEMA = R"sql(
            -- Database-wide settings, e.g. ssh_connect (the user@host of the
            -- remote, set by create()).
            CREATE TABLE settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );

            -- Files and directories to be synchronized. directory is a
            -- boolean: the paths are directories to be scanned rather than a
            -- single file.
            CREATE TABLE sync (
                client_path TEXT PRIMARY KEY,
                server_path TEXT NOT NULL,
                directory INTEGER NOT NULL
            );

            -- Metadata of each file in a synchronized directory as of the last
            -- sync. client_path is the directory and rpath the file's path
            -- relative to it.
            CREATE TABLE files (
                client_path TEXT NOT NULL,
                rpath TEXT NOT NULL,
                hash TEXT,
                size INTEGER,
                mtime INTEGER,
                permissions INTEGER,
                PRIMARY KEY (client_path, rpath)
            );

            -- Operations in progress, kept for recovery after a crash or error.
            -- An entry is removed once the file has been transferred, its hash
            -- verified and the rename has succeeded. direction is 'push'
            -- (client to server) or 'pull' (server to client).
            CREATE TABLE pending_journal (
                id INTEGER PRIMARY KEY,
                client_path TEXT NOT NULL,
                direction TEXT NOT NULL CHECK (direction IN ('push', 'pull')),
                expected_hash TEXT,
                status TEXT NOT NULL,
                created_at INTEGER
            );
        )sql";
    }

    Database::Database() = default;

    Database::~Database()
    {
        close();
    }

    ExpectBool Database::open(const std::string& path)
    {
        return open_with_flags(path, SQLITE_OPEN_READWRITE);
    }

    ExpectBool Database::create(const std::string& path, const std::string& ssh_connect)
    {
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
            return std::unexpected(Error{"Cannot create database " + path + ": file already exists", EEXIST, ErrorKind::Errno});
        if (ec)
            return std::unexpected(Error{"Cannot create database " + path + ": " + ec.message(), ec.value(), ErrorKind::Errno});

        auto opened = open_with_flags(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        if (!opened)
            return opened;

        auto init = initialize(ssh_connect);
        if (!init)
        {
            // Don't leave an empty database behind: it would make the next
            // create() fail with "file already exists".
            close();
            std::filesystem::remove(path, ec);
            return std::unexpected(Error{"Cannot initialize database " + path + ": " + init.error().err,
                                         init.error().ecode, init.error().kind});
        }
        return true;
    }

    ExpectBool Database::add(const std::string& client_path, const std::string& server_path)
    {
        if (db_ == nullptr)
            return std::unexpected(Error{"Cannot add " + client_path + ": database is not open", EINVAL, ErrorKind::Errno});

        // Follows symlinks, so a link to a directory counts as a directory.
        std::error_code ec;
        auto status = std::filesystem::status(client_path, ec);
        if (ec)
            return std::unexpected(Error{"Cannot add " + client_path + ": " + ec.message(), ec.value(), ErrorKind::Errno});

        int directory;
        if (std::filesystem::is_directory(status))
            directory = 1;
        else if (std::filesystem::is_regular_file(status))
            directory = 0;
        else
            return std::unexpected(Error{"Cannot add " + client_path + ": not a regular file or directory", EINVAL, ErrorKind::Errno});

        // A duplicate client_path violates the primary key and is an error.
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, "INSERT INTO sync (client_path, server_path, directory) VALUES (?1, ?2, ?3)", -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
            rc = bind_text(stmt, 1, client_path);
        if (rc == SQLITE_OK)
            rc = bind_text(stmt, 2, server_path);
        if (rc == SQLITE_OK)
            rc = sqlite3_bind_int(stmt, 3, directory);
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                rc = SQLITE_OK;
        }
        if (rc != SQLITE_OK)
        {
            std::string msg = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return std::unexpected(Error{"Cannot add " + client_path + ": " + msg, rc, ErrorKind::Sqlite});
        }
        sqlite3_finalize(stmt);
        return true;
    }

    int Database::bind_text(sqlite3_stmt* stmt, int idx, const std::string& value)
    {
        return sqlite3_bind_text(stmt, idx, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    namespace
    {
        std::string column_string(sqlite3_stmt* stmt, int col)
        {
            const unsigned char* text = sqlite3_column_text(stmt, col);
            return text ? std::string(reinterpret_cast<const char*>(text), sqlite3_column_bytes(stmt, col)) : std::string();
        }
    }

    ExpectSyncItems Database::sync_items()
    {
        if (db_ == nullptr)
            return std::unexpected(Error{"Cannot read sync table: database is not open", EINVAL, ErrorKind::Errno});

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, "SELECT client_path, server_path, directory FROM sync ORDER BY client_path", -1, &stmt, nullptr);
        std::vector<SyncItem> items;
        while (rc == SQLITE_OK && (rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            items.push_back({column_string(stmt, 0), column_string(stmt, 1), sqlite3_column_int(stmt, 2) != 0});
            rc = SQLITE_OK;
        }
        if (rc != SQLITE_DONE)
        {
            std::string msg = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return std::unexpected(Error{"Cannot read sync table: " + msg, rc, ErrorKind::Sqlite});
        }
        sqlite3_finalize(stmt);
        return items;
    }

    ExpectFileRecords Database::files_in(const std::string& client_path)
    {
        if (db_ == nullptr)
            return std::unexpected(Error{"Cannot read files of " + client_path + ": database is not open", EINVAL, ErrorKind::Errno});

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, "SELECT rpath, hash, size, mtime, permissions FROM files WHERE client_path = ?1 ORDER BY rpath", -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
            rc = bind_text(stmt, 1, client_path);
        std::vector<FileRecord> records;
        while (rc == SQLITE_OK && (rc = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            FileRecord r;
            r.rpath = column_string(stmt, 0);
            r.hash = column_string(stmt, 1);
            r.size = sqlite3_column_int64(stmt, 2);
            r.mtime = sqlite3_column_int64(stmt, 3);
            r.permissions = sqlite3_column_int(stmt, 4);
            records.push_back(std::move(r));
            rc = SQLITE_OK;
        }
        if (rc != SQLITE_DONE)
        {
            std::string msg = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return std::unexpected(Error{"Cannot read files of " + client_path + ": " + msg, rc, ErrorKind::Sqlite});
        }
        sqlite3_finalize(stmt);
        return records;
    }

    ExpectBool Database::put_files(const std::string& client_path, const std::vector<FileRecord>& records)
    {
        if (db_ == nullptr)
            return std::unexpected(Error{"Cannot write files of " + client_path + ": database is not open", EINVAL, ErrorKind::Errno});

        if (auto r = exec("BEGIN"); !r)
            return r;

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_,
            "INSERT OR REPLACE INTO files (client_path, rpath, hash, size, mtime, permissions) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            -1, &stmt, nullptr);
        for (size_t i = 0; rc == SQLITE_OK && i < records.size(); ++i)
        {
            const FileRecord& rec = records[i];
            rc = bind_text(stmt, 1, client_path);
            if (rc == SQLITE_OK)
                rc = bind_text(stmt, 2, rec.rpath);
            if (rc == SQLITE_OK)
                rc = rec.hash.empty() ? sqlite3_bind_null(stmt, 3) : bind_text(stmt, 3, rec.hash);
            if (rc == SQLITE_OK)
                rc = sqlite3_bind_int64(stmt, 4, rec.size);
            if (rc == SQLITE_OK)
                rc = sqlite3_bind_int64(stmt, 5, rec.mtime);
            if (rc == SQLITE_OK)
                rc = sqlite3_bind_int(stmt, 6, rec.permissions);
            if (rc == SQLITE_OK)
            {
                rc = sqlite3_step(stmt);
                if (rc == SQLITE_DONE)
                    rc = SQLITE_OK;
            }
            if (rc == SQLITE_OK)
                rc = sqlite3_reset(stmt);
        }
        if (rc != SQLITE_OK)
        {
            std::string msg = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            exec("ROLLBACK");
            return std::unexpected(Error{"Cannot write files of " + client_path + ": " + msg, rc, ErrorKind::Sqlite});
        }
        sqlite3_finalize(stmt);
        return exec("COMMIT");
    }

    ExpectBool Database::initialize(const std::string& ssh_connect)
    {
        if (auto r = exec("BEGIN"); !r)
            return r;
        if (auto r = exec(SCHEMA); !r)
            return r;
        if (auto r = set_setting("ssh_connect", ssh_connect); !r)
        {
            exec("ROLLBACK");
            return r;
        }
        return exec("COMMIT");
    }

    ExpectBool Database::set_setting(const std::string& key, const std::string& value)
    {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO settings (key, value) VALUES (?1, ?2)", -1, &stmt, nullptr);
        if (rc == SQLITE_OK)
            rc = sqlite3_bind_text(stmt, 1, key.c_str(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
        if (rc == SQLITE_OK)
            rc = sqlite3_bind_text(stmt, 2, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (rc == SQLITE_OK)
        {
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE)
                rc = SQLITE_OK;
        }
        if (rc != SQLITE_OK)
        {
            // Read the message before finalizing, which may reset it.
            std::string msg = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return std::unexpected(Error{"Cannot set " + key + ": " + msg, rc, ErrorKind::Sqlite});
        }
        sqlite3_finalize(stmt);
        return true;
    }

    ExpectBool Database::open_with_flags(const std::string& path, int flags)
    {
        close();

        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
        if (rc != SQLITE_OK)
        {
            // sqlite3_open_v2 usually allocates a handle even on failure; it
            // carries the error message and must still be closed.
            std::string msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
            sqlite3_close_v2(db);
            return std::unexpected(Error{"Cannot open database " + path + ": " + msg, rc, ErrorKind::Sqlite});
        }

        // Turn on extended result codes so errors are more specific.
        sqlite3_extended_result_codes(db, 1);

        db_ = db;
        path_ = path;
        return true;
    }

    ExpectBool Database::exec(const char* sql)
    {
        char* errmsg = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK)
        {
            std::string msg = errmsg ? errmsg : sqlite3_errstr(rc);
            sqlite3_free(errmsg);
            // Ends a transaction the failed statement left open; harmless if
            // there is none.
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected(Error{std::move(msg), rc, ErrorKind::Sqlite});
        }
        return true;
    }

    bool Database::is_open() const
    {
        return db_ != nullptr;
    }

    void Database::close()
    {
        if (db_ == nullptr)
            return;

        // sqlite3_close_v2 defers the close until any unfinalized statements
        // are finalized, so the handle can always be dropped here.
        sqlite3_close_v2(db_);
        db_ = nullptr;
        path_.clear();
    }
}
