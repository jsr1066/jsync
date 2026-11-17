#include "commands.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

static void write_file(const fs::path& p, const std::string& content)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

int main()
{
    fs::path root = fs::temp_directory_path() / ("jsync-test-hash-" + std::to_string(::getpid()));
    fs::remove_all(root);

    write_file(root / "empty", "");
    write_file(root / "abc", "abc");
    write_file(root / "sub/fox", "The quick brown fox jumps over the lazy dog");
    // 56 bytes: exercises the padding boundary (length does not fit in the same block).
    write_file(root / "boundary56", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    // One million 'a' characters: standard FIPS test vector, spans many blocks and buffer reads.
    write_file(root / "million_a", std::string(1000000, 'a'));

    Commands cmd;
    std::string h;

    auto check_hash = [&](const std::string& rpath, const std::string& expected)
    {
        auto res = cmd.hash(root.string(), rpath, h);
        CHECK(res.has_value());
        CHECK(h == expected);
    };

    check_hash("empty", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check_hash("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    check_hash("sub/fox", "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
    check_hash("boundary56", "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    check_hash("million_a", "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    // Errors leave hash empty.
    CHECK(!cmd.hash(root.string(), "nope", h).has_value());
    CHECK(h.empty());
    CHECK(!cmd.hash(root.string(), "sub", h).has_value());          // directory
    CHECK(!cmd.hash(root.string(), "../abc", h).has_value());       // traversal
    CHECK(!cmd.hash(root.string(), "/etc/hosts", h).has_value());   // absolute
    CHECK(!cmd.hash(root.string(), "", h).has_value());

    fs::remove_all(root);
    if (failures == 0)
    {
        std::cout << "all hash tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
