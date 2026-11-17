#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace jsync_ns
{
    // Incremental SHA-1 (FIPS 180-1).
    class Sha1
    {
    public:
        Sha1();
        void update(const void* data, size_t len);
        // Finalizes and returns the 40 character lowercase hex digest.
        std::string hex_digest();

    private:
        void process_block(const uint8_t* block);

        uint32_t state_[5];
        uint64_t total_bytes_;
        uint8_t buffer_[64];
        size_t buffered_;
    };
}
