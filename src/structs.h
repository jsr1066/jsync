#pragma once

#include <cstddef>
#include <ctime>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include "sha1.h"

namespace jsync_ns
{
    // How Error::ecode is to be interpreted.
    enum class ErrorKind
    {
        Errno,  // an errno value (EINVAL, ENOENT, ...)
        Sqlite, // an SQLite result code (SQLITE_CANTOPEN, ...), possibly extended
        Regex,  // a std::regex_constants::error_type value (error_paren, ...)
    };

    struct Error
    {
        std::string err;
        int ecode;
        ErrorKind kind;
    };

    struct ScanInfo
    {
        std::string directory;
        std::string rpath;
        int permission;
        size_t size;
        time_t mtime;
    };

    // REQ-SCAN: the server directory to scan and regexes of files to skip.
    struct ScanRequest
    {
        std::string directory;
        std::vector<std::string> excludes;
    };

    // RSP-SCAN: the final message of a scan.
    struct ScanResponse
    {
        bool ok = true;
        std::string text;
    };

    // State for reading or writing  a file chunk by chunk. Owns the file descriptor and
    // closes it on destruction, so it is move-only.
    struct IOContext
    {
        int fd = -1;
        std::string directory;
        std::string rpath;
        size_t total_size = 0;

        // Write state (unused when reading): the hash the finished file must
        // have, the bytes and running hash accumulated so far, and the temp file
        // being written.
        std::string expected_hash;
        size_t bytes_done = 0;
        Sha1 sha;
        std::string temp_path;

        IOContext() = default;
        IOContext(const IOContext&) = delete;
        IOContext& operator=(const IOContext&) = delete;
        IOContext(IOContext&& other) noexcept
            : fd(other.fd)
            , directory(std::move(other.directory))
            , rpath(std::move(other.rpath))
            , total_size(other.total_size)
            , expected_hash(std::move(other.expected_hash))
            , bytes_done(other.bytes_done)
            , sha(other.sha)
            , temp_path(std::move(other.temp_path))
        {
            other.fd = -1;
        }
        IOContext& operator=(IOContext&& other) noexcept
        {
            if (this != &other)
            {
                if (fd >= 0)
                {
                    ::close(fd);
                }
                fd = other.fd;
                directory = std::move(other.directory);
                rpath = std::move(other.rpath);
                total_size = other.total_size;
                expected_hash = std::move(other.expected_hash);
                bytes_done = other.bytes_done;
                sha = other.sha;
                temp_path = std::move(other.temp_path);
                other.fd = -1;
            }
            return *this;
        }
        ~IOContext()
        {
            if (fd >= 0)
            {
                ::close(fd);
            }
        }
    };

    constexpr size_t MAX_CHUNK = 1024*512;

    struct Chunk
    {
        std::string directory;
        std::string rpath;
        size_t total_size;
        size_t chunk_size;
        char bytes[MAX_CHUNK];
    };

    using ExpectBool = std::expected<bool, Error>;
    using ExpectIOContext = std::expected<IOContext, Error>;
    using ExpectChunk = std::expected<Chunk, Error>;

}

