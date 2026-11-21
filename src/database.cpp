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

            -- Metadata for each file or directory being synchronized.
            -- directory is a boolean: client_path is a directory to be scanned
            -- rather than a single file.
            CREATE TABLE files (
                client_path TEXT PRIMARY KEY,
                server_path TEXT,
                hash TEXT,
                size INTEGER,
                mtime INTEGER,
                permissions INTEGER,
                directory INTEGER
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
