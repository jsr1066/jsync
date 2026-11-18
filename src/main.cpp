#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "file_ops.h"
#include "peer.h"
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

    // Frames "cmd=RSP-SCAN;status=...;[text=...;]" (the terminating response).
    std::vector<uint8_t> scan_complete_wire(const std::string& status, const std::string& text)
    {
        std::string body = "cmd=RSP-SCAN;status=" + status + ";";
        if (!text.empty())
        {
            body += "text=" + text + ";";
        }
        char header[32];
        int n = std::snprintf(header, sizeof header, "JS1.0;%08zu;", body.size());
        std::vector<uint8_t> out(header, header + n);
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    int test_scan_client(Peer& peer)
    {
        std::vector<uint8_t> wire;
        if (!convertToWire(ScanRequest{SCAN_DIR, {}}, wire) || !peer.send(wire.data(), wire.size()))
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
            if (command != "RSP-SCAN")
            {
                std::fprintf(stderr, "unexpected response: %s\n", command.c_str());
                return 1;
            }
            auto status = kv.find("status");
            if (status != kv.end())
            {
                const std::string& text = kv["text"];
                if (status->second != "complete")
                {
                    std::fprintf(stderr, "scan failed: %s\n", text.c_str());
                    return 1;
                }
                std::printf("scan complete: %zu files%s%s\n", count, text.empty() ? "" : " - ", text.c_str());
                return 0;
            }
            ++count;
            std::printf("%s size=%s mtime=%s permission=%s\n", kv["file"].c_str(), kv["size"].c_str(),
                        kv["mtime"].c_str(), kv["permission"].c_str());
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
            auto wire = scan_complete_wire("error", "expected REQ-SCAN with directory");
            peer.send(wire.data(), wire.size());
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
            auto wire = scan_complete_wire("error", r.error().err);
            peer.send(wire.data(), wire.size());
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
        auto wire = scan_complete_wire("complete", "");
        if (!peer.send(wire.data(), wire.size()))
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
    return test_scan(opts);
}
