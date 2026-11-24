#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "database.h"
#include "file_ops.h"
#include "peer.h"
#include "scan.h"
#include "wire.h"

using namespace jsync_ns;

namespace
{
    constexpr char DEFAULT_HOST[] = "jeff@192.168.1.145";

    struct Options
    {
        bool server = false;
    };

    void usage(const char* prog)
    {
        std::fprintf(stderr,
                     "usage: %s [--server]\n"
                     "  --server   run as the server (stdin/stdout carry the protocol)\n"
                     "             without it, connect to %s as a client\n",
                     prog, DEFAULT_HOST);
    }

    bool parse_args(int argc, char** argv, Options& opts)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--server") == 0)
            {
                opts.server = true;
            }
            else
            {
                std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
                return false;
            }
        }
        return true;
    }

    // Server sends a length-prefixed message; client receives and prints it.
    int test_startup(const Options& opts)
    {
        Peer peer;

        if (opts.server)
        {
            if (!peer.accept())
            {
                return 1;
            }

            const std::string msg = "hello from the jsync server";
            uint32_t len = static_cast<uint32_t>(msg.size());
            if (!peer.send(&len, sizeof(len)) || !peer.send(msg.data(), msg.size()))
            {
                return 1;
            }
            spdlog::info("sent test message ({} bytes)", msg.size());
            return 0;
        }

        if (!peer.connect(DEFAULT_HOST))
        {
            return 1;
        }

        uint32_t len = 0;
        if (!peer.receive(&len, sizeof(len)))
        {
            return 1;
        }
        std::string msg(len, '\0');
        if (!peer.receive(msg.data(), len))
        {
            return 1;
        }
        std::printf("received: %s\n", msg.c_str());
        return 0;
    }

    constexpr char SCAN_DIR[] = "/home/jeff/Projects/jsync";

    // Sends the terminating RSP-SCAN. Newlines in text are flattened so it always fits the wire.
    bool send_scan_response(Peer& peer, bool ok, std::string text)
    {
        for (char& c : text)
        {
            if (c == '\n' || c == FIELD_SEP)
            {
                c = ' ';
            }
        }
        std::vector<uint8_t> wire;
        return convertToWire(ScanResponse{ok, std::move(text)}, wire) && peer.send(wire.data(), wire.size());
    }

    int test_scan_client(Peer& peer)
    {
        std::vector<uint8_t> wire;
        if (!convertToWire(ScanRequest{SCAN_DIR, {".git", ".obsi*"}}, wire) || !peer.send(wire.data(), wire.size()))
        {
            return 1;
        }

        size_t count = 0;
        for (;;)
        {
            std::string command;
            std::map<std::string, std::string> kv;
            std::vector<uint8_t> chunk;
            if (!read_command(peer, command, kv, chunk))
            {
                return 1;
            }
            if (command == "DATA-SCAN")
            {
                ++count;
                std::printf("%s size=%s mtime=%s permission=%s\n", kv["file"].c_str(), kv["size"].c_str(),
                            kv["mtime"].c_str(), kv["permission"].c_str());
                continue;
            }
            if (command != "RSP-SCAN")
            {
                std::fprintf(stderr, "unexpected response: %s\n", command.c_str());
                return 1;
            }
            const std::string& text = kv["text"];
            if (kv["status"] != "complete")
            {
                std::fprintf(stderr, "scan failed: %s\n", text.c_str());
                return 1;
            }
            std::printf("scan complete: %zu files%s%s\n", count, text.empty() ? "" : " - ", text.c_str());
            return 0;
        }
    }

    int test_scan_server(Peer& peer)
    {
        std::string command;
        std::map<std::string, std::string> kv;
        std::vector<uint8_t> chunk;
        if (!read_command(peer, command, kv, chunk))
        {
            return 1;
        }
        if (command != "REQ-SCAN" || !kv.contains("directory"))
        {
            send_scan_response(peer, false, "expected REQ-SCAN with directory");
            return 1;
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
        if (auto r = ops.scan(kv["directory"], excludes, results); !r)
        {
            send_scan_response(peer, false, r.error().err);
            return 1;
        }

        for (const ScanInfo& info : results)
        {
            std::vector<uint8_t> wire;
            if (!convertToWire(info, wire) || !peer.send(wire.data(), wire.size()))
            {
                return 1;
            }
        }
        if (!send_scan_response(peer, true, ""))
        {
            return 1;
        }
        spdlog::info("scan sent {} entries", results.size());
        return 0;
    }

    int test_scan(const Options& opts)
    {
        Peer peer;
        if (opts.server)
        {
            if (!peer.accept())
            {
                return 1;
            }
            return test_scan_server(peer);
        }
        if (!peer.connect(DEFAULT_HOST))
        {
            return 1;
        }
        return test_scan_client(peer);
    }

    constexpr char PLAN_DB[] = "/Users/jeff/jsync.sq3";
    constexpr char PLAN_CLIENT_PATH[] = "/Users/jeff/Projects/jsync";
    constexpr char PLAN_SERVER_PATH[] = "/home/jeff/Projects/jsync2";

    const char* action_name(ActionType type)
    {
        switch (type)
        {
        case ActionType::ReadFromServer: return "read from server";
        case ActionType::WriteToServer:  return "write to server";
        case ActionType::DeleteLocal:    return "delete local";
        case ActionType::DeleteServer:   return "delete server";
        case ActionType::Conflict:       return "conflict";
        }
        return "?";
    }

    // Client: opens (or creates) the database, makes sure the test directory is
    // in the sync table, scans both sides and prints the planned actions.
    // Server: answers the scan requests until the client disconnects.
    int test_plan(const Options& opts)
    {
        Peer peer;
        if (opts.server)
        {
            if (!peer.accept())
            {
                return 1;
            }
            while (test_scan_server(peer) == 0)
            {
            }
            return 0;
        }

        Database db;
        auto opened = std::filesystem::exists(PLAN_DB) ? db.open(PLAN_DB) : db.create(PLAN_DB, DEFAULT_HOST);
        if (!opened)
        {
            std::fprintf(stderr, "database %s: %s\n", PLAN_DB, opened.error().err.c_str());
            return 1;
        }
        // Fails if the directory is already in the table (a rerun); that is fine.
        if (auto r = db.add(PLAN_CLIENT_PATH, PLAN_SERVER_PATH, {".trash"}); !r)
        {
            std::fprintf(stderr, "add: %s (continuing)\n", r.error().err.c_str());
        }

        if (!peer.connect(DEFAULT_HOST))
        {
            return 1;
        }

        Scan scan(db, peer);
        auto actions = scan.plan();
        if (!actions)
        {
            std::fprintf(stderr, "plan failed: %s\n", actions.error().err.c_str());
            return 1;
        }
        for (const Action& a : *actions)
        {
            std::printf("%-16s %s\n", action_name(a.type), a.rpath.c_str());
        }
        std::printf("%zu actions\n", actions->size());
        return 0;
    }
}

int main(int argc, char** argv)
{
    Options opts;
    if (!parse_args(argc, argv, opts))
    {
        usage(argv[0]);
        return 2;
    }

    (void)&test_startup;
    (void)&test_scan;
    return test_plan(opts);
}
