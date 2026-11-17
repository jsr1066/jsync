#include "peer.h"

#include <spdlog/spdlog.h>

namespace jsync_ns
{
    namespace
    {
        // Logs the error for the named command and wraps it for return.
        std::unexpected<Error> fail(const char* command, const asio::error_code& ec)
        {
            spdlog::error("{}: {} (code {})", command, ec.message(), ec.value());
            return std::unexpected(Error{ec.message(), ec.value()});
        }
    }

    Peer::Peer()
        : socket_(io_)
    {
    }

    ExpectBool Peer::listen(uint16_t port)
    {
        if (socket_.is_open())
        {
            return fail("listen", asio::error::already_connected);
        }

        asio::error_code ec;
        asio::ip::tcp::acceptor acceptor(io_);
        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);

        acceptor.open(endpoint.protocol(), ec);
        if (ec) return fail("listen", ec);
        acceptor.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec) return fail("listen", ec);
        acceptor.bind(endpoint, ec);
        if (ec) return fail("listen", ec);
        acceptor.listen(1, ec);
        if (ec) return fail("listen", ec);

        spdlog::info("listening on port {}", port);
        acceptor.accept(socket_, ec);
        if (ec) return fail("listen", ec);

        spdlog::info("accepted connection from {}", remote_address());
        return true;
    }

    ExpectBool Peer::connect(const std::string& host, uint16_t port)
    {
        if (socket_.is_open())
        {
            return fail("connect", asio::error::already_connected);
        }

        asio::error_code ec;
        asio::ip::tcp::resolver resolver(io_);
        auto endpoints = resolver.resolve(host, std::to_string(port), ec);
        if (ec) return fail("connect", ec);

        asio::connect(socket_, endpoints, ec);
        if (ec)
        {
            asio::error_code ignored;
            socket_.close(ignored);
            return fail("connect", ec);
        }

        spdlog::info("connected to {}", remote_address());
        return true;
    }

    bool Peer::is_connected() const
    {
        return socket_.is_open();
    }

    std::string Peer::remote_address() const
    {
        asio::error_code ec;
        auto ep = socket_.remote_endpoint(ec);
        if (ec) return {};
        return ep.address().to_string() + ":" + std::to_string(ep.port());
    }

    void Peer::close()
    {
        asio::error_code ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}
