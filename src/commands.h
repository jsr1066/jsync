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
        ExpectBool hash(std::string& directory, std::string& rpath, std::string& hash);
        ExpectReadContext open(std::string& directory , std::string& rpath);
        ExpectBool read(const ReadContext&, Chunk&);
    };

    std::string tokv(const ScanInfo&);
    std::string tokv(const Chunk&);

}

