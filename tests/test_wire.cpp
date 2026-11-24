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
    std::string body = "cmd=DATA-SCAN\020file=sub/a.txt\020size=1234\020mtime=1700000000\020permission=420\020status=continue\020";
    char len[16];
    std::snprintf(len, sizeof len, "%08zu", body.size());
    CHECK(s == "JS1.0\020" + std::string(len) + "\020" + body);
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
        CHECK(command == "DATA-SCAN");
        CHECK(kv.count("directory") == 0);
        CHECK(kv["file"] == "sub/a.txt");
        CHECK(kv["size"] == "1234");
        CHECK(kv["mtime"] == "1700000000");
        CHECK(kv["permission"] == "420");
        CHECK(kv["status"] == "continue");
        CHECK(chunk.empty());
    }

    // Final RSP-SCAN.
    {
        std::vector<uint8_t> rsp;
        CHECK(convertToWire(ScanResponse{true, ""}, rsp).has_value());
        std::string rb = "cmd=RSP-SCAN\020status=complete\020";
        std::snprintf(len, sizeof len, "%08zu", rb.size());
        CHECK(std::string(rsp.begin(), rsp.end()) == "JS1.0\020" + std::string(len) + "\020" + rb);

        CHECK(convertToWire(ScanResponse{false, "boom"}, rsp).has_value());
        rb = "cmd=RSP-SCAN\020status=fail\020text=boom\020";
        std::snprintf(len, sizeof len, "%08zu", rb.size());
        CHECK(std::string(rsp.begin(), rsp.end()) == "JS1.0\020" + std::string(len) + "\020" + rb);
    }

    // REQ-SCAN.
    {
        ScanRequest req;
        req.directory = "/srv/data";
        req.excludes = {"\\.git/.*", "a{1,3}\\.o$"};
        CHECK(convertToWire(req, out).has_value());
        std::string rb = std::string("cmd=REQ-SCAN\020directory=/srv/data\020excludes=") + "\\.git/.*" + EXCLUDE_SEP + "a{1,3}\\.o$\020";
        std::snprintf(len, sizeof len, "%08zu", rb.size());
        CHECK(std::string(out.begin(), out.end()) == "JS1.0\020" + std::string(len) + "\020" + rb);

        req.excludes.clear();
        CHECK(convertToWire(req, out).has_value());
        CHECK(std::string(out.begin(), out.end()).find("excludes") == std::string::npos);

        req.excludes = {std::string("a") + FIELD_SEP + "b"};
        CHECK(!convertToWire(req, out).has_value());
        req.excludes = {"a\nb"};
        CHECK(!convertToWire(req, out).has_value());
        req.excludes = {std::string("a") + EXCLUDE_SEP + "b"};
        CHECK(!convertToWire(req, out).has_value());
        req.excludes.clear();
        req.directory = std::string("x") + FIELD_SEP + "y";
        CHECK(!convertToWire(req, out).has_value());
    }

    // REQ-WRITE.
    {
        WriteRequest req;
        req.directory = "/srv/data";
        req.rpath = "sub/a.txt";
        req.hash = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
        req.total_size = 1234;
        req.mtime = 1700000000;
        req.permission = 0640;
        CHECK(convertToWire(req, out).has_value());
        std::string rb = "cmd=REQ-WRITE\020directory=/srv/data\020file=sub/a.txt\020hash=da39a3ee5e6b4b0d3255bfef95601890afd80709\020total_size=1234\020mtime=1700000000\020permission=416\020";
        std::snprintf(len, sizeof len, "%08zu", rb.size());
        CHECK(std::string(out.begin(), out.end()) == "JS1.0\020" + std::string(len) + "\020" + rb);

        req.rpath = std::string("a") + FIELD_SEP + "b";
        CHECK(!convertToWire(req, out).has_value());
    }

    // RSP-READ, success and failure.
    {
        ReadResponse rsp;
        rsp.directory = "/srv/data";
        rsp.rpath = "sub/a.txt";
        rsp.hash = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
        rsp.mtime = 1700000000;
        rsp.permission = 0640;
        CHECK(convertToWire(rsp, out).has_value());
        std::string rb = "cmd=RSP-READ\020directory=/srv/data\020file=sub/a.txt\020hash=da39a3ee5e6b4b0d3255bfef95601890afd80709\020mtime=1700000000\020permission=416\020status=complete\020";
        std::snprintf(len, sizeof len, "%08zu", rb.size());
        CHECK(std::string(out.begin(), out.end()) == "JS1.0\020" + std::string(len) + "\020" + rb);

        rsp.ok = false;
        rsp.text = "no such file";
        CHECK(convertToWire(rsp, out).has_value());
        rb = "cmd=RSP-READ\020directory=/srv/data\020file=sub/a.txt\020status=fail\020text=no such file\020";
        std::snprintf(len, sizeof len, "%08zu", rb.size());
        CHECK(std::string(out.begin(), out.end()) == "JS1.0\020" + std::string(len) + "\020" + rb);

        rsp.text = std::string("x") + FIELD_SEP + "y";
        CHECK(!convertToWire(rsp, out).has_value());
    }

    // convertFromWire: round trips and malformed input.
    {
        const std::string H = "da39a3ee5e6b4b0d3255bfef95601890afd80709";

        WriteRequest w;
        w.directory = "/srv/data"; w.rpath = "sub/a.txt"; w.hash = H;
        w.total_size = 1234; w.mtime = 1700000000; w.permission = 0640;
        WriteRequest w2;
        KeyValues kv{{"directory", w.directory}, {"file", w.rpath}, {"hash", H}, {"total_size", "1234"},
                     {"mtime", "1700000000"}, {"permission", "416"}};
        CHECK(convertFromWire(kv, w2).has_value());
        CHECK(w2.directory == w.directory && w2.rpath == w.rpath && w2.hash == H);
        CHECK(w2.total_size == 1234 && w2.mtime == 1700000000 && w2.permission == 0640);

        // A bad write request fails and leaves the target untouched.
        WriteRequest keep = w2;
        for (const char* key : {"directory", "file", "hash", "total_size", "mtime", "permission"})
        {
            KeyValues m = kv;
            m.erase(key);
            CHECK(!convertFromWire(m, keep).has_value());
        }
        for (auto bad : {std::pair<const char*, const char*>{"hash", "xyz"}, {"hash", "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709"},
                         {"total_size", "-1"}, {"total_size", "12x"}, {"permission", "99999"}, {"permission", "-1"},
                         {"mtime", ""}, {"mtime", "1.5"}})
        {
            KeyValues m = kv;
            m[bad.first] = bad.second;
            CHECK(!convertFromWire(m, keep).has_value());
        }
        CHECK(keep.rpath == "sub/a.txt" && keep.total_size == 1234);

        // RSP-READ.
        ReadResponse r;
        KeyValues rk{{"directory", "/srv/data"}, {"file", "a"}, {"hash", H}, {"mtime", "5"},
                     {"permission", "420"}, {"status", "complete"}};
        CHECK(convertFromWire(rk, r).has_value());
        CHECK(r.ok && r.hash == H && r.mtime == 5 && r.permission == 0644 && r.rpath == "a");
        rk.erase("hash");
        CHECK(!convertFromWire(rk, r).has_value());
        KeyValues fk{{"directory", "/srv/data"}, {"file", "a"}, {"status", "fail"}, {"text", "nope"}};
        CHECK(convertFromWire(fk, r).has_value());
        CHECK(!r.ok && r.text == "nope" && r.hash.empty());
        fk["status"] = "continue";
        CHECK(!convertFromWire(fk, r).has_value());

        // DATA-SCAN keeps the directory the caller set.
        ScanInfo si;
        si.directory = "/keep";
        CHECK(convertFromWire(KeyValues{{"file", "a/b"}, {"size", "7"}, {"mtime", "-3"}, {"permission", "420"}}, si).has_value());
        CHECK(si.directory == "/keep" && si.rpath == "a/b" && si.size == 7 && si.mtime == -3 && si.permission == 420);
        CHECK(!convertFromWire(KeyValues{{"file", "a"}, {"size", "7"}, {"mtime", "1"}}, si).has_value());

        // RSP-SCAN.
        ScanResponse sr;
        CHECK(convertFromWire(KeyValues{{"status", "fail"}, {"text", "boom"}}, sr).has_value());
        CHECK(!sr.ok && sr.text == "boom");
        CHECK(convertFromWire(KeyValues{{"status", "complete"}}, sr).has_value() && sr.ok && sr.text.empty());
        CHECK(!convertFromWire(KeyValues{}, sr).has_value());

        // REQ-SCAN: excludes split, round trip with the builder.
        ScanRequest q;
        q.directory = "/srv/data";
        q.excludes = {"\\.git/.*", "a{1,3}\\.o$"};
        std::vector<uint8_t> wire;
        CHECK(convertToWire(q, wire).has_value());
        std::string wb(wire.begin() + 15, wire.end());
        KeyValues qk;
        for (size_t pos = 0; pos < wb.size();)
        {
            size_t e = wb.find(FIELD_SEP, pos);
            std::string f = wb.substr(pos, e - pos);
            size_t eq = f.find('=');
            qk[f.substr(0, eq)] = f.substr(eq + 1);
            pos = e + 1;
        }
        ScanRequest q2;
        CHECK(convertFromWire(qk, q2).has_value());
        CHECK(q2.directory == q.directory && q2.excludes == q.excludes);
        qk.erase("excludes");
        CHECK(convertFromWire(qk, q2).has_value() && q2.excludes.empty());
        qk.erase("directory");
        CHECK(!convertFromWire(qk, q2).has_value());
    }

    // Delimiters in values are rejected.
    ScanInfo bad = info;
    bad.rpath = std::string("a") + FIELD_SEP + "b";
    CHECK(!convertToWire(bad, out).has_value());
    bad.rpath = "a\nb";
    CHECK(!convertToWire(bad, out).has_value());

    if (failures == 0) std::cout << "all wire tests passed\n";
    return failures == 0 ? 0 : 1;
}
