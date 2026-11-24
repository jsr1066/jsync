#include "peer.h"
#include "file_ops.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

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

static std::string slurp(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// Creates path with content, mtime and permission.
static void make_file(const fs::path& p, const std::string& content, time_t mtime, mode_t mode)
{
    fs::create_directories(p.parent_path());
    { std::ofstream(p, std::ios::binary) << content; }
    struct timeval tv[2] = {{mtime, 0}, {mtime, 0}};
    ::utimes(p.c_str(), tv);
    ::chmod(p.c_str(), mode);
}

// True if p has the given content, mtime and permission.
static bool is_file(const fs::path& p, const std::string& content, time_t mtime, mode_t mode)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && st.st_mtime == mtime && (st.st_mode & 07777) == mode && slurp(p) == content;
}

// The server loop: answers requests until the client disconnects or the stream breaks.
static void serve(Peer& p)
{
    for (;;)
    {
        std::string command;
        std::map<std::string, std::string> kv;
        std::vector<uint8_t> chunk;
        if (!read_command(p, command, kv, chunk))
        {
            return;
        }
        if (command == "REQ-READ" ? !p.handle_read(kv) : command == "REQ-WRITE" ? !p.handle_write(kv) : true)
        {
            return;
        }
    }
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "jsync_test_transfer";
    fs::remove_all(root);
    const fs::path client = root / "client", server = root / "server";
    fs::create_directories(client);
    fs::create_directories(server);

    int a_to_b[2], b_to_a[2];
    CHECK(::pipe(a_to_b) == 0);
    CHECK(::pipe(b_to_a) == 0);
    Peer c, s;
    std::thread t1([&] { c.attach(b_to_a[0], a_to_b[1], true); });
    std::thread t2([&] { s.attach(a_to_b[0], b_to_a[1], true); });
    t1.join();
    t2.join();
    CHECK(c.is_connected() && s.is_connected());
    std::thread srv([&] { serve(s); });

    const std::string big = make_data(MAX_CHUNK * 2 + 123);

    // Client to server: multi-chunk file into a new subdirectory.
    make_file(client / "sub/big", big, 1700000000, 0640);
    CHECK(c.write_file(client.string(), server.string(), "sub/big").has_value());
    CHECK(is_file(server / "sub/big", big, 1700000000, 0640));
    CHECK(!fs::exists(server / "sub/.big.jsync.tmp"));

    // Server to client, including replacing an existing file.
    make_file(server / "down", big, 1600000000, 0600);
    make_file(client / "down", "old", 1500000000, 0644);
    CHECK(c.read_file(server.string(), client.string(), "down").has_value());
    CHECK(is_file(client / "down", big, 1600000000, 0600));
    CHECK(!fs::exists(client / ".down.jsync.tmp"));

    // A file that is exactly one chunk, and a small one.
    make_file(client / "one", make_data(MAX_CHUNK), 1700000001, 0644);
    CHECK(c.write_file(client.string(), server.string(), "one").has_value());
    CHECK(is_file(server / "one", make_data(MAX_CHUNK), 1700000001, 0644));
    make_file(server / "small", "hello", 1700000002, 0755);
    CHECK(c.read_file(server.string(), client.string(), "small").has_value());
    CHECK(is_file(client / "small", "hello", 1700000002, 0755));

    // Empty files, both ways.
    make_file(client / "empty_up", "", 1700000003, 0644);
    CHECK(c.write_file(client.string(), server.string(), "empty_up").has_value());
    CHECK(is_file(server / "empty_up", "", 1700000003, 0644));
    make_file(server / "empty_down", "", 1700000004, 0644);
    CHECK(c.read_file(server.string(), client.string(), "empty_down").has_value());
    CHECK(is_file(client / "empty_down", "", 1700000004, 0644));

    // Failures reported by the server leave the connection usable.
    CHECK(!c.read_file(server.string(), client.string(), "missing").has_value());
    CHECK(!fs::exists(client / "missing"));
    CHECK(!c.read_file(server.string(), client.string(), "../etc").has_value());

    // The server cannot create the file: the parent "directory" is a regular file.
    make_file(server / "blocker", "x", 1700000000, 0644);
    make_file(client / "f", big, 1700000005, 0644);
    CHECK(!c.write_file(client.string(), (server / "blocker").string(), "f").has_value());
    // ...and the stream is still in step afterwards.
    CHECK(c.write_file(client.string(), server.string(), "f").has_value());
    CHECK(is_file(server / "f", big, 1700000005, 0644));
    CHECK(c.read_file(server.string(), client.string(), "small").has_value());

    // A local file that does not exist fails before anything is sent.
    CHECK(!c.write_file(client.string(), server.string(), "nope").has_value());
    CHECK(c.read_file(server.string(), client.string(), "one").has_value());
    CHECK(is_file(client / "one", make_data(MAX_CHUNK), 1700000001, 0644));

    c.close();
    srv.join();
    s.close();
    fs::remove_all(root);

    if (failures == 0) std::cout << "all transfer tests passed\n";
    return failures == 0 ? 0 : 1;
}
