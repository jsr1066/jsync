#pragma once

#include <string>
#include <vector>

#include "structs.h"

namespace jsync_ns
{
    class Commands
    {
    public:
        ExpectBool scan(const std::string& directory, const std::vector<std::string>& excludes, std::vector<ScanInfo>& results);
        // Computes the SHA-1 of directory/rpath; hash receives 40 lowercase hex chars.
        ExpectBool hash(const std::string& directory, const std::string& rpath, std::string& hash);
        // Opens directory/rpath for reading. The returned context owns the fd
        // and closes it when destroyed.
        ExpectReadContext open(const std::string& directory, const std::string& rpath);
        // Fills chunk with the next up to MAX_CHUNK bytes. Returns true if the
        // chunk holds data (call again for more), false at EOF (chunk_size == 0).
        ExpectBool read(const ReadContext& context, Chunk& chunk);
    };

    std::string tokv(const ScanInfo&);
    std::string tokv(const Chunk&);

}

