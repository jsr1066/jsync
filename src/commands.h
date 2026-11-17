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
        ExpectReadContext open(std::string& directory , std::string& rpath);
        ExpectBool read(const ReadContext&, Chunk&);
    };

    std::string tokv(const ScanInfo&);
    std::string tokv(const Chunk&);

}

