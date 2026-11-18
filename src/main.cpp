#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <spdlog/spdlog.h>

#include "peer.h"

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
}

int main(int argc, char** argv)
{
    Options opts;
    if (!parse_args(argc, argv, opts))
    {
        usage(argv[0]);
        return 2;
    }

    return test_startup(opts);
}
