#include "file_ops.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>

#include <sys/stat.h>

namespace fs = std::filesystem;
using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

static std::string make_data(size_t n)
{
    std::string s(n, '\0');
    for (size_t i = 0; i < n; i++)
    {
        s[i] = static_cast<char>((i * 31 + i / 251) & 0xFF);
    }
    return s;
}

static std::string sha_of(const std::string& data)
{
    Sha1 sha;
    sha.update(data.data(), data.size());
    return sha.hex_digest();
}

static std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// Feeds data to write() in pieces of `piece` bytes; returns the last result.
static ExpectBool write_all(FileOps& cmd, IOContext& ctx, const std::string& data, size_t piece)
{
    auto chunk = std::make_unique<Chunk>();
    ExpectBool result = true;
    size_t off = 0;
    do
    {
        size_t n = std::min(piece, data.size() - off);
        chunk->chunk_size = n;
        std::copy(data.data() + off, data.data() + off + n, chunk->bytes);
        off += n;
        result = cmd.write(ctx, *chunk);
    } while (result && *result && off < data.size());
    return result;
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "jsync_test_write";
    fs::remove_all(root);
    fs::create_directories(root);
    FileOps cmd;
    const std::string dir = root.string();

    // Multi-chunk write into a new subdirectory.
    {
        std::string data = make_data(MAX_CHUNK * 2 + 123);
        auto ctx = cmd.wopen(dir, "sub/big", data.size(), sha_of(data), 1700000000, 0640);
        CHECK(ctx.has_value());
        auto r = write_all(cmd, *ctx, data, MAX_CHUNK);
        CHECK(r.has_value() && !*r);
        CHECK(fs::exists(root / "sub" / ".big.jsync.tmp"));
        CHECK(!fs::exists(root / "sub" / "big"));
        CHECK(slurp(root / "sub" / ".big.jsync.tmp") == data);
    }

    // Small odd-sized pieces.
    {
        std::string data = make_data(1000);
        auto ctx = cmd.wopen(dir, "small", data.size(), sha_of(data), 1700000000, 0640);
        CHECK(ctx.has_value());
        auto r = write_all(cmd, *ctx, data, 77);
        CHECK(r.has_value() && !*r);
        CHECK(slurp(root / ".small.jsync.tmp") == data);
    }

    // Empty file: a single empty chunk completes it.
    {
        auto ctx = cmd.wopen(dir, "empty", 0, sha_of(""), 1700000000, 0640);
        CHECK(ctx.has_value());
        auto r = write_all(cmd, *ctx, "", 1);
        CHECK(r.has_value() && !*r);
        CHECK(fs::exists(root / ".empty.jsync.tmp"));
    }

    // Hash mismatch removes the temp file.
    {
        std::string data = make_data(500);
        auto ctx = cmd.wopen(dir, "bad", data.size(), sha_of("other"), 1700000000, 0640);
        CHECK(ctx.has_value());
        auto r = write_all(cmd, *ctx, data, 500);
        CHECK(!r.has_value());
        CHECK(!fs::exists(root / ".bad.jsync.tmp"));
    }

    // Writing more than total_size is rejected.
    {
        std::string data = make_data(100);
        auto ctx = cmd.wopen(dir, "over", 50, sha_of(data), 1700000000, 0640);
        CHECK(ctx.has_value());
        auto r = write_all(cmd, *ctx, data, 100);
        CHECK(!r.has_value());
    }

    // rename() moves the verified temp file over the target, replacing it.
    {
        { std::ofstream(root / "small", std::ios::binary) << "old contents"; }
        auto r = cmd.rename(dir, "small");
        CHECK(r.has_value() && *r);
        CHECK(!fs::exists(root / ".small.jsync.tmp"));
        CHECK(slurp(root / "small") == make_data(1000));
        // The mtime and permission given to wopen survives write() and rename().
        struct stat st;
        CHECK(::stat((root / "small").c_str(), &st) == 0 && st.st_mtime == 1700000000);
        CHECK((st.st_mode & 07777) == 0640);

        r = cmd.rename(dir, "sub/big");
        CHECK(r.has_value());
        CHECK(slurp(root / "sub" / "big") == make_data(MAX_CHUNK * 2 + 123));
    }

    // rename() errors: no temp file, bad paths.
    CHECK(!cmd.rename(dir, "small").has_value());
    CHECK(!cmd.rename(dir, "nope").has_value());
    CHECK(!cmd.rename(dir, "../x").has_value());
    CHECK(!cmd.rename(dir, "/etc/x").has_value());
    CHECK(!cmd.rename(dir, "").has_value());

    // wopen() path errors.
    CHECK(!cmd.wopen(dir, "../x", 0, "", 0, 0).has_value());
    CHECK(!cmd.wopen(dir, "/etc/x", 0, "", 0, 0).has_value());
    CHECK(!cmd.wopen(dir, "", 0, "", 0, 0).has_value());

    fs::remove_all(root);
    if (failures == 0)
    {
        std::cout << "test_write: all passed\n";
    }
    return failures == 0 ? 0 : 1;
}
