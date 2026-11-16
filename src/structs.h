#pragma once

#include <expected>

namespace jsync_ns
{
    struct Error
    {
        std::string err;
        int ecode;
    };

    struct ScanInfo
    {
        std::string directory
        std::string rpath
        int permission;
        size_t size;
        time_t mtime;
    };

    struct ReadContext
    {
        int fd;
        std::string directory;
        std::string rpath;
        size_t total_size;
    }

    const_expr size_t MAX_CHUNK = 4096;

    struct Chunk
    {
        std::string directory;
        std::string rpath;
        size_t total_size;
        size_t chunk_size;
        char bytes[MAX_CHUNK];
    }

    using ExpectBool = std::expected<bool, Error>;
    using ExpectReadContext = std::expected<bool, ReadContext>;
    using ExpectChunk = std::expected<bool, Chunk>;

}

