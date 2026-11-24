#include "file_ops.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
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

static std::string make_data(size_t n)
{
    std::string s(n, '\0');
    for (size_t i = 0; i < n; i++)
    {
        s[i] = static_cast<char>((i * 31 + i / 251) & 0xFF);
    }
    return s;
}

// Reads the whole file through ropen()/read(); returns the contents and chunk count.
static bool read_all(FileOps& cmd, const std::string& dir, const std::string& rpath,
                     std::string& out, int& chunks, size_t& total, std::string& hash)
{
    auto ctx = cmd.ropen(dir, rpath);
    if (!ctx)
    {
        return false;
    }
    total = ctx->total_size;
    struct stat st;
    CHECK(::stat((fs::path(dir) / rpath).c_str(), &st) == 0);
    CHECK(ctx->mtime == st.st_mtime);
    CHECK(ctx->permission == static_cast<int>(st.st_mode & 07777));
    auto chunk = std::make_unique<Chunk>();
    out.clear();
    hash.clear();
    chunks = 0;
    while (true)
    {
        auto more = cmd.read(*ctx, *chunk);
        if (!more)
        {
            return false;
        }
        if (!*more)
        {
            CHECK(chunk->chunk_size == 0);
            // The EOF chunk repeats the hash reported by the last data chunk.
            CHECK(!chunk->hash.empty());
            CHECK(hash.empty() || hash == chunk->hash);
            hash = chunk->hash;
            return true;
        }
        CHECK(chunk->rpath == rpath);
        CHECK(chunk->directory == dir);
        CHECK(chunk->total_size == total);
        CHECK(chunk->mtime == st.st_mtime);
        CHECK(chunk->permission == static_cast<int>(st.st_mode & 07777));
        out.append(chunk->bytes, chunk->chunk_size);
        chunks++;
        // The hash is only reported on the chunk that completes the file.
        CHECK(chunk->hash.empty() == (out.size() < total));
        if (!chunk->hash.empty())
        {
            hash = chunk->hash;
        }
    }
}

int main()
{
    fs::path root = fs::temp_directory_path() / ("jsync-test-read-" + std::to_string(::getpid()));
    fs::remove_all(root);

    const std::string small = "hello world\n";
    const std::string exact = make_data(MAX_CHUNK);          // exactly one full chunk
    const std::string multi = make_data(MAX_CHUNK * 2 + 17); // three chunks, last partial
    write_file(root / "empty", "");
    write_file(root / "small", small);
    write_file(root / "sub/exact", exact);
    write_file(root / "multi", multi);

    FileOps cmd;
    std::string out;
    int chunks = 0;
    size_t total = 0;
    std::string hash;
    std::string expected;

    // chunk.hash must match FileOps::hash for the same file.
    auto check_hash = [&](const char* rpath)
    {
        CHECK(cmd.hash(root.string(), rpath, expected).has_value());
        CHECK(hash.size() == 40);
        CHECK(hash == expected);
    };

    CHECK(read_all(cmd, root.string(), "empty", out, chunks, total, hash));
    CHECK(out.empty() && chunks == 0 && total == 0);
    check_hash("empty");

    CHECK(read_all(cmd, root.string(), "small", out, chunks, total, hash));
    CHECK(out == small && chunks == 1 && total == small.size());
    check_hash("small");

    CHECK(read_all(cmd, root.string(), "sub/exact", out, chunks, total, hash));
    CHECK(out == exact && chunks == 1 && total == exact.size());
    check_hash("sub/exact");

    CHECK(read_all(cmd, root.string(), "multi", out, chunks, total, hash));
    CHECK(out == multi && chunks == 3 && total == multi.size());
    check_hash("multi");

    // ropen() errors.
    CHECK(!cmd.ropen(root.string(), "nope").has_value());
    CHECK(!cmd.ropen(root.string(), "sub").has_value());        // directory
    CHECK(!cmd.ropen(root.string(), "../small").has_value());   // traversal
    CHECK(!cmd.ropen(root.string(), "/etc/hosts").has_value()); // absolute
    CHECK(!cmd.ropen(root.string(), "").has_value());

    // Context owns the fd: it is closed on destruction and a moved-from context is inert.
    {
        auto ctx = cmd.ropen(root.string(), "small");
        CHECK(ctx.has_value());
        int fd = ctx->fd;
        CHECK(fd >= 0);
        {
            IOContext moved = std::move(*ctx);
            CHECK(ctx->fd == -1);
            CHECK(moved.fd == fd);
        }
        CHECK(::fcntl(fd, F_GETFD) == -1 && errno == EBADF);
    }

    fs::remove_all(root);
    if (failures == 0)
    {
        std::cout << "all read tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
