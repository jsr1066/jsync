#include "wire.h"
#include "peer.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

int main()
{
    ScanInfo info;
    info.directory = "/srv/data";
    info.rpath = "sub/a.txt";
    info.permission = 420;
    info.size = 1234;
    info.mtime = 1700000000;

    std::vector<uint8_t> out;
    CHECK(convertToWire(info, out).has_value());
    std::string s(out.begin(), out.end());
    std::string body = "cmd=RSP-SCAN;file=sub/a.txt;size=1234;mtime=1700000000;permission=420;";
    char len[16];
    std::snprintf(len, sizeof len, "%08zu", body.size());
    CHECK(s == "JS1.0;" + std::string(len) + ";" + body);
    CHECK(s.size() == 15 + body.size());

    // Round trip through read_command.
    {
        int a_to_b[2], b_to_a[2];
        CHECK(::pipe(a_to_b) == 0);
        CHECK(::pipe(b_to_a) == 0);
        Peer a, b;
        std::thread ta([&] { a.attach(b_to_a[0], a_to_b[1], true); });
        std::thread tb([&] { b.attach(a_to_b[0], b_to_a[1], true); });
        ta.join();
        tb.join();

        CHECK(a.send(out.data(), out.size()).has_value());
        std::string command;
        std::map<std::string, std::string> kv;
        std::vector<uint8_t> chunk;
        CHECK(read_command(b, command, kv, chunk).has_value());
        CHECK(command == "RSP-SCAN");
        CHECK(kv.count("directory") == 0);
        CHECK(kv["file"] == "sub/a.txt");
        CHECK(kv["size"] == "1234");
        CHECK(kv["mtime"] == "1700000000");
        CHECK(kv["permission"] == "420");
        CHECK(chunk.empty());
    }

    // REQ-SCAN.
    {
        ScanRequest req;
        req.directory = "/srv/data";
        req.excludes = {"\\.git/.*", "a{1,3}\\.o$"};
        CHECK(convertToWire(req, out).has_value());
        std::string rb = std::string("cmd=REQ-SCAN;directory=/srv/data;excludes=") + "\\.git/.*" + EXCLUDE_SEP + "a{1,3}\\.o$;";
        std::snprintf(len, sizeof len, "%08zu", rb.size());
        CHECK(std::string(out.begin(), out.end()) == "JS1.0;" + std::string(len) + ";" + rb);

        req.excludes.clear();
        CHECK(convertToWire(req, out).has_value());
        CHECK(std::string(out.begin(), out.end()).find("excludes") == std::string::npos);

        req.excludes = {"a;b"};
        CHECK(!convertToWire(req, out).has_value());
        req.excludes = {"a\nb"};
        CHECK(!convertToWire(req, out).has_value());
        req.excludes = {std::string("a") + EXCLUDE_SEP + "b"};
        CHECK(!convertToWire(req, out).has_value());
        req.excludes.clear();
        req.directory = "x;y";
        CHECK(!convertToWire(req, out).has_value());
    }

    // Delimiters in values are rejected.
    ScanInfo bad = info;
    bad.rpath = "a;b";
    CHECK(!convertToWire(bad, out).has_value());
    bad.rpath = "a\nb";
    CHECK(!convertToWire(bad, out).has_value());

    if (failures == 0) std::cout << "all wire tests passed\n";
    return failures == 0 ? 0 : 1;
}
