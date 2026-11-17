#pragma once

#include "structs.h"

namespace jsync_ns
{
    class Command
    {
        ExpectBool scan(std::string& directory , std::vector<ScanInfo>&);
        ExpectBool hash(std::string& directory , std::string& rpath, std::string& hash);
        ExpectReadContext open(std::string& directory , std::string& rpath);
        ExpectBool read(const ReadContext&, Chunk&);
    };

    std::string tokv(const ScanInfo&);
    std::string tokv(const Chunk&);
    
}

