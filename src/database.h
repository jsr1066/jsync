#pragma once

#include <string>

#include "structs.h"

struct sqlite3;
struct sqlite3_stmt;

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

        // Adds a file or directory to be synchronized to the sync table.
        // client_path must exist locally; sync.directory is set from whether
        // it is a directory or a regular file. Fails if it is neither, does not
        // exist, or is already in the table.
        ExpectBool add(const std::string& client_path, const std::string& server_path);

        // Returns every row of the sync table, ordered by client_path.
        ExpectSyncItems sync_items();

        // Returns the stored metadata of every file in the directory
        // client_path (a sync item with directory true), ordered by rpath.
        ExpectFileRecords files_in(const std::string& client_path);

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

        // Binds text to parameter idx of stmt.
        static int bind_text(sqlite3_stmt* stmt, int idx, const std::string& value);

        sqlite3* db_ = nullptr;
        std::string path_;
    };
}
