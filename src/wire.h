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

    // Terminates every field on the wire (DLE, 0x10). It is a control
    // character, so it can't appear in a file name.
    constexpr char FIELD_SEP = '\x10';

    // Converts to the wire format described in Commands.md:
    // "JS1.0<FS><8 digit length><FS>cmd=...<FS>key=value<FS>...". out is
    // replaced. An error is returned if a value can't be represented (e.g.
    // contains FIELD_SEP).

    // Separates the regexes inside the excludes value of REQ-SCAN. FIELD_SEP
    // can't be used (it ends a field) and regexes may contain ',' or '|'.
    constexpr char EXCLUDE_SEP = '\x1f';

    // Builds a REQ-SCAN: cmd=REQ-SCAN<FS>directory=<d><FS>excludes=<r1><US><r2>...;
    // The excludes key is omitted when the list is empty.
    ExpectBool convertToWire(const ScanRequest&, std::vector<uint8_t>& out);

    // Builds a DATA-SCAN entry (one per file, status=continue).
    ExpectBool convertToWire(const ScanInfo&, std::vector<uint8_t>& out);

    // Builds the final RSP-SCAN (status=complete|fail, text if non-empty).
    ExpectBool convertToWire(const ScanResponse&, std::vector<uint8_t>& out);
}
