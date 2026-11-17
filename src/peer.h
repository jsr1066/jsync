#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <sys/types.h>

#include "structs.h"

namespace jsync_ns
{
    // One end of a symmetric connection between two hosts, carried over a pair
    // of file descriptors (one to read, one to write). The client side spawns
    // "ssh host jsync --server" and uses its pipes; the server side uses its own
    // stdin and stdout. Once connected the two ends are equivalent. All calls
    // block.
    class Peer
    {
    public:
        Peer();
        ~Peer();
        Peer(const Peer&) = delete;
        Peer& operator=(const Peer&) = delete;

        // Client side: spawns "ssh -T host jsync --server" and handshakes.
        ExpectBool connect(const std::string& host);

        // Server side: uses stdin/stdout as the stream and handshakes. stdout
        // carries the protocol, so the default logger is switched to stderr.
        ExpectBool accept();

        // Spawns an arbitrary command and handshakes over its stdin/stdout.
        // connect() is built on this; tests use it with local commands.
        ExpectBool spawn(const std::vector<std::string>& argv);

        // Uses existing descriptors as the stream and handshakes. If owned, the
        // descriptors are closed by close().
        ExpectBool attach(int read_fd, int write_fd, bool owned);

        // Writes exactly n bytes.
        ExpectBool send(const void* buf, size_t n);

        // Reads exactly n bytes; an error is returned if the stream ends first.
        ExpectBool receive(void* buf, size_t n);

        bool is_connected() const;

        // Closes the stream (giving the remote EOF) and reaps any spawned child.
        void close();

    private:
        ExpectBool handshake();
        // Waits for the child to exit (killing it after a timeout) and returns
        // a description of how it ended; empty if there is no child.
        std::string reap();

        int read_fd_ = -1;
        int write_fd_ = -1;
        bool owned_ = false;
        pid_t child_ = -1;
    };
}
