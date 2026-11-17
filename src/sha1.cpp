#include "sha1.h"

#include <cstring>

namespace jsync_ns
{
    namespace
    {
        inline uint32_t rol(uint32_t v, unsigned n)
        {
            return (v << n) | (v >> (32 - n));
        }
    }

    Sha1::Sha1()
        : state_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}
        , total_bytes_(0)
        , buffer_{}
        , buffered_(0)
    {
    }

    void Sha1::process_block(const uint8_t* block)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
        {
            w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16)
                 | (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++)
        {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
        for (int i = 0; i < 80; i++)
        {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }

            uint32_t temp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    void Sha1::update(const void* data, size_t len)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        total_bytes_ += len;

        if (buffered_ > 0)
        {
            size_t take = 64 - buffered_;
            if (take > len)
            {
                take = len;
            }
            std::memcpy(buffer_ + buffered_, p, take);
            buffered_ += take;
            p += take;
            len -= take;
            if (buffered_ == 64)
            {
                process_block(buffer_);
                buffered_ = 0;
            }
        }
        while (len >= 64)
        {
            process_block(p);
            p += 64;
            len -= 64;
        }
        if (len > 0)
        {
            std::memcpy(buffer_, p, len);
            buffered_ = len;
        }
    }

    std::string Sha1::hex_digest()
    {
        uint64_t bit_len = total_bytes_ * 8;

        const uint8_t pad_start = 0x80;
        update(&pad_start, 1);
        const uint8_t zero = 0;
        while (buffered_ != 56)
        {
            update(&zero, 1);
        }
        uint8_t len_bytes[8];
        for (int i = 0; i < 8; i++)
        {
            len_bytes[i] = static_cast<uint8_t>(bit_len >> (56 - 8 * i));
        }
        update(len_bytes, 8);

        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(40);
        for (int i = 0; i < 5; i++)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                out.push_back(hex[(state_[i] >> shift) & 0xF]);
            }
        }
        return out;
    }
}
