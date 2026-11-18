#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "structs.h"

namespace jsync_ns
{
    // Largest command body (the bytes after the 15 byte header) either side
    // will send or accept.
    constexpr size_t MAX_COMMAND_LEN = 64 * 1024;

    // Converts to the wire format described in Commands.md:
    // "JS1.0;<8 digit length>;cmd=...;key=value;...". out is replaced. An error
    // is returned if a value can't be represented (e.g. contains ';').

    // Separates the regexes inside the excludes value of REQ-SCAN. ';' can't be
    // used (it ends a field) and regexes may contain ',' or '|'.
    constexpr char EXCLUDE_SEP = '\x1f';

    // Builds a REQ-SCAN: cmd=REQ-SCAN;directory=<d>;excludes=<r1><US><r2>...;
    // The excludes key is omitted when the list is empty.
    ExpectBool convertToWire(const ScanRequest&, std::vector<uint8_t>& out);

    // Builds an RSP-SCAN entry.
    ExpectBool convertToWire(const ScanInfo&, std::vector<uint8_t>& out);
}
