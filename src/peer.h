#pragma once

#include <cstdint>
#include <string>

#include <asio.hpp>

#include "structs.h"

namespace jsync_ns
{
    // One end of a symmetric connection between two hosts. Either side may
    // establish it: one calls listen(), the other calls connect(). Once
    // connected the two ends are equivalent. All calls block.
    class Peer
    {
    public:
        Peer();

        // Binds to port and blocks until a remote peer connects. Only one
        // connection is accepted; the listening socket is closed afterwards.
        ExpectBool listen(uint16_t port);

        // Resolves host and blocks until connected to host:port.
        ExpectBool connect(const std::string& host, uint16_t port);

        bool is_connected() const;

        // Address of the remote end ("ip:port"), or empty if not connected.
        std::string remote_address() const;

        void close();

    private:
        asio::io_context io_;
        asio::ip::tcp::socket socket_;
    };
}
