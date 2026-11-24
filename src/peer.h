#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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
        // carries the protocol, so the default logger is switched to
        // ~/.local/state/jsync/jsync.log (or stderr if that file can't be opened).
        ExpectBool accept();

        // Uses existing descriptors as the stream and handshakes. If owned, the
        // descriptors are closed by close().
        ExpectBool attach(int read_fd, int write_fd, bool owned);

        // Writes exactly n bytes.
        ExpectBool send(const void* buf, size_t n);

        // Reads exactly n bytes; an error is returned if the stream ends first.
        ExpectBool receive(void* buf, size_t n);

        // Client side: copies server_dir/rpath to local_dir/rpath. Sends REQ-READ
        // and writes the DATA-CHUNKs to a temp file; once RSP-READ confirms the
        // hash, mtime and permission it renames the file into place. A failure
        // reported by the server (RSP-READ status=fail) leaves the connection
        // usable; any other error means the stream is out of step and the
        // connection should be closed. The temp file is removed on error.
        ExpectBool read_file(const std::string& server_dir, const std::string& local_dir, const std::string& rpath);

        // Client side: copies local_dir/rpath to server_dir/rpath. Sends REQ-WRITE
        // and the DATA-CHUNKs, then waits for RSP-WRITE. A failure reported by the
        // server leaves the connection usable; a local read error part way through
        // does not (the server is left waiting for data).
        ExpectBool write_file(const std::string& local_dir, const std::string& server_dir, const std::string& rpath);

        // Server side: answers a REQ-READ whose key/value pairs read_command
        // returned, sending DATA-CHUNKs then RSP-READ. A request that cannot be
        // satisfied is answered with RSP-READ status=fail and counts as handled;
        // an error is returned only if the connection itself fails.
        ExpectBool handle_read(const std::map<std::string, std::string>& kv);

        // Server side: answers a REQ-WRITE by receiving its DATA-CHUNKs (reading
        // them from the stream), then replies RSP-WRITE. If the file cannot be
        // written the chunks are still consumed so the stream stays in step, and
        // RSP-WRITE status=fail is sent. An error is returned only if the
        // connection fails or the request is too malformed to know how much data follows.
        ExpectBool handle_write(const std::map<std::string, std::string>& kv);

        bool is_connected() const;

        // Closes the stream (giving the remote EOF) and reaps any spawned child.
        void close();

    public: // public for unit tests
        // Spawns an arbitrary command and handshakes over its stdin/stdout.
        // connect() is built on this; tests use it with local commands.
        ExpectBool spawn(const std::vector<std::string>& argv);

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

    // Reads one wire message (see Commands.md): the 15 byte "JS1.0;<len>;" header,
    // <len> bytes of key=value pairs, then chunk_size bytes of chunk data if the
    // chunk_size key is present. command is set from the cmd key.
    ExpectBool read_command(Peer&, std::string& command, std::map<std::string, std::string>& kv, std::vector<uint8_t>& chunk);
}
