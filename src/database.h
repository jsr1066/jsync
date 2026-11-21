#pragma once

#include <string>

#include "structs.h"

struct sqlite3;

namespace jsync_ns
{
    // Interface to the jsync SQLite database. Owns a single connection, which
    // is opened by open() and closed by close() or the destructor.
    class Database
    {
    public:
        Database();
        ~Database();
        Database(const Database&) = delete;
        Database& operator=(const Database&) = delete;

        // Opens an existing database at path; fails if it does not exist.
        ExpectBool open(const std::string& path);

        // Creates a new database at path, with the schema from
        // jsync-design/database.md, stores ssh_connect (the user@host of
        // the remote) in settings, and opens it. Fails if path already exists;
        // if initialization fails the new file is removed.
        ExpectBool create(const std::string& path, const std::string& ssh_connect);

        bool is_open() const;

        // Closes the connection; does nothing if not open.
        void close();

    private:
        // Shared by open() and create(); flags are passed to sqlite3_open_v2.
        ExpectBool open_with_flags(const std::string& path, int flags);

        // Runs sql, which may hold several statements, on the open connection.
        // If it fails, any transaction it left open is rolled back.
        ExpectBool exec(const char* sql);

        // Creates the schema and stores ssh_connect, in one transaction.
        ExpectBool initialize(const std::string& ssh_connect);

        // Inserts or replaces a row in settings.
        ExpectBool set_setting(const std::string& key, const std::string& value);

        sqlite3* db_ = nullptr;
        std::string path_;
    };
}
