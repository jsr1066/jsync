#include "scan.h"
#include "wire.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <sys/time.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

static ScanInfo si(const std::string& rpath, size_t size, time_t mtime)
{
    return ScanInfo{"", rpath, 0644, size, mtime};
}

static FileRecord rec(const std::string& rpath, long long size, long long mtime)
{
    FileRecord r;
    r.rpath = rpath;
    r.size = size;
    r.mtime = mtime;
    return r;
}

static std::string show(const std::vector<Action>& actions)
{
    static const char* names[] = {"read", "write", "dellocal", "delserver", "conflict"};
    std::string out;
    for (const Action& a : actions)
    {
        out += std::string(names[static_cast<int>(a.type)]) + ":" + a.rpath + " ";
    }
    return out;
}

static void test_compare()
{
    SyncItem item{"/local", "/remote", true, {}};
    auto run = [&](std::vector<ScanInfo> l, std::vector<ScanInfo> s, std::vector<FileRecord> d)
    {
        return show(Scan::compare(item, l, s, d));
    };

    // all present
    CHECK(run({si("a", 1, 10)}, {si("a", 1, 10)}, {rec("a", 1, 10)}) == "");
    CHECK(run({si("a", 2, 20)}, {si("a", 1, 10)}, {rec("a", 1, 10)}) == "write:a ");
    CHECK(run({si("a", 1, 10)}, {si("a", 2, 20)}, {rec("a", 1, 10)}) == "read:a ");
    CHECK(run({si("a", 2, 20)}, {si("a", 3, 30)}, {rec("a", 1, 10)}) == "conflict:a ");
    CHECK(run({si("a", 2, 20)}, {si("a", 2, 20)}, {rec("a", 1, 10)}) == "");
    // no record
    CHECK(run({si("a", 1, 10)}, {si("a", 1, 10)}, {}) == "");
    CHECK(run({si("a", 1, 10)}, {si("a", 2, 10)}, {}) == "conflict:a ");
    CHECK(run({si("a", 1, 10)}, {}, {}) == "write:a ");
    CHECK(run({}, {si("a", 1, 10)}, {}) == "read:a ");
    // deleted on one side
    CHECK(run({si("a", 1, 10)}, {}, {rec("a", 1, 10)}) == "dellocal:a ");
    CHECK(run({si("a", 2, 20)}, {}, {rec("a", 1, 10)}) == "conflict:a ");
    CHECK(run({}, {si("a", 1, 10)}, {rec("a", 1, 10)}) == "delserver:a ");
    CHECK(run({}, {si("a", 2, 20)}, {rec("a", 1, 10)}) == "conflict:a ");
    // gone everywhere
    CHECK(run({}, {}, {rec("a", 1, 10)}) == "");
    // ordering by rpath, and item fields copied
    auto acts = Scan::compare(item, {si("b", 1, 1), si("a", 1, 1)}, {}, {});
    CHECK(acts.size() == 2 && acts[0].rpath == "a" && acts[1].rpath == "b");
    CHECK(acts[0].client_path == "/local" && acts[0].server_path == "/remote");
}

// Serves one REQ-SCAN on peer, like the real server will.
static void serve_scan(Peer& peer)
{
    std::string command;
    std::map<std::string, std::string> kv;
    std::vector<uint8_t> chunk;
    if (!read_command(peer, command, kv, chunk) || command != "REQ-SCAN")
    {
        return;
    }
    std::vector<std::string> excludes;
    if (auto it = kv.find("excludes"); it != kv.end())
    {
        size_t pos = 0;
        const std::string& v = it->second;
        while (pos <= v.size())
        {
            size_t end = v.find(EXCLUDE_SEP, pos);
            if (end == std::string::npos)
            {
                end = v.size();
            }
            if (end > pos)
            {
                excludes.push_back(v.substr(pos, end - pos));
            }
            pos = end + 1;
        }
    }
    FileOps ops;
    std::vector<ScanInfo> results;
    auto r = ops.scan(kv["directory"], excludes, results);
    std::vector<uint8_t> wire;
    if (r)
    {
        for (const ScanInfo& info : results)
        {
            if (convertToWire(info, wire))
            {
                peer.send(wire.data(), wire.size());
            }
        }
    }
    convertToWire(ScanResponse{static_cast<bool>(r), r ? "" : r.error().err}, wire);
    peer.send(wire.data(), wire.size());
}

static void touch(const fs::path& p, const std::string& content, long long mtime)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
    struct timeval tv[2] = {{static_cast<time_t>(mtime), 0}, {static_cast<time_t>(mtime), 0}};
    ::utimes(p.c_str(), tv);
}

static void test_plan()
{
    fs::path root = fs::temp_directory_path() / ("jsync-test-scanplan-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::path local = root / "local", remote = root / "remote";
    fs::create_directories(local);
    fs::create_directories(remote);

    // same:      unchanged both sides
    // newlocal:  only local          -> write
    // newremote: only remote         -> read
    // dellocal:  gone remote         -> delete local
    // delremote: gone local          -> delete server
    // modlocal:  size changed local  -> write
    // skipme.tmp excluded
    touch(local / "same", "abc", 1000);
    touch(remote / "same", "abc", 1000);
    touch(local / "newlocal", "x", 1000);
    touch(remote / "newremote", "y", 1000);
    touch(local / "dellocal", "z", 1000);
    touch(remote / "delremote", "z", 1000);
    touch(local / "modlocal", "longer now", 2000);
    touch(remote / "modlocal", "old", 1000);
    touch(local / "skipme.tmp", "t", 1000);

    std::string db_path = (root / "test.db").string();
    Database db;
    CHECK(db.create(db_path, "user@host"));
    CHECK(db.add(local.string(), remote.string(), {"\\.tmp$"}));
    CHECK(db.put_files(local.string(), {rec("same", 3, 1000), rec("dellocal", 1, 1000),
                                        rec("delremote", 1, 1000), rec("modlocal", 3, 1000)}));

    Peer client, server;
    int c2s[2], s2c[2];
    CHECK(::pipe(c2s) == 0 && ::pipe(s2c) == 0);
    std::thread t([&] {
        if (server.attach(c2s[0], s2c[1], true))
        {
            serve_scan(server);
        }
    });
    CHECK(client.attach(s2c[0], c2s[1], true));

    Scan scan(db, client);
    auto actions = scan.plan();
    CHECK(actions.has_value());
    if (actions)
    {
        CHECK(show(*actions) == "dellocal:dellocal delserver:delremote write:modlocal write:newlocal read:newremote ");
    }
    client.close();
    t.join();
    fs::remove_all(root);
}

int main()
{
    test_compare();
    test_plan();
    if (failures)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
