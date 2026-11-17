#pragma once

#include <cstddef>
#include <ctime>
#include <expected>
#include <string>
#include <utility>

#include <unistd.h>

namespace jsync_ns
{
    struct Error
    {
        std::string err;
        int ecode;
    };

    struct ScanInfo
    {
        std::string directory;
        std::string rpath;
        int permission;
        size_t size;
        time_t mtime;
    };

    // State for reading a file chunk by chunk. Owns the file descriptor and
    // closes it on destruction, so it is move-only.
    struct ReadContext
    {
        int fd = -1;
        std::string directory;
        std::string rpath;
        size_t total_size = 0;

        ReadContext() = default;
        ReadContext(const ReadContext&) = delete;
        ReadContext& operator=(const ReadContext&) = delete;
        ReadContext(ReadContext&& other) noexcept
            : fd(other.fd)
            , directory(std::move(other.directory))
            , rpath(std::move(other.rpath))
            , total_size(other.total_size)
        {
            other.fd = -1;
        }
        ReadContext& operator=(ReadContext&& other) noexcept
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
                other.fd = -1;
            }
            return *this;
        }
        ~ReadContext()
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
    using ExpectReadContext = std::expected<ReadContext, Error>;
    using ExpectChunk = std::expected<Chunk, Error>;

}

