#include "peer.h"

#include <chrono>
#include <iostream>
#include <thread>

using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

static constexpr uint16_t TEST_PORT = 45871;

int main()
{
    // Listener in a background thread, connector in the main thread.
    {
        Peer server;
        Peer client;
        ExpectBool server_result = false;

        std::thread t([&] { server_result = server.listen(TEST_PORT); });

        ExpectBool client_result = std::unexpected(Error{"not run", -1});
        for (int i = 0; i < 50; ++i)   // retry until the listener is up
        {
            client_result = client.connect("127.0.0.1", TEST_PORT);
            if (client_result) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        t.join();

        CHECK(client_result.has_value());
        CHECK(server_result.has_value());
        CHECK(client.is_connected());
        CHECK(server.is_connected());
        CHECK(!client.remote_address().empty());
        CHECK(!server.remote_address().empty());

        // A connected peer cannot connect or listen again.
        CHECK(!client.connect("127.0.0.1", TEST_PORT).has_value());
        CHECK(!client.listen(TEST_PORT).has_value());

        client.close();
        server.close();
        CHECK(!client.is_connected());
    }

    // Connecting to a port nobody listens on fails cleanly.
    {
        Peer p;
        auto r = p.connect("127.0.0.1", 45872);
        CHECK(!r.has_value());
        CHECK(!p.is_connected());
    }

    if (failures == 0) std::cout << "all peer tests passed\n";
    return failures == 0 ? 0 : 1;
}
