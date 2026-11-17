#include "peer.h"

#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

using namespace jsync_ns;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::cerr << __FILE__ << ":" << __LINE__ << " CHECK failed: " #cond "\n"; ++failures; } } while (0)

int main()
{
    // Echo through a spawned cat: the handshake and a small message round-trip.
    {
        Peer p;
        auto r = p.spawn({"cat"});
        CHECK(r.has_value());
        CHECK(p.is_connected());
        CHECK(p.send("hello", 5).has_value());
        char buf[5];
        CHECK(p.receive(buf, 5).has_value());
        CHECK(std::memcmp(buf, "hello", 5) == 0);

        // Already connected.
        CHECK(!p.spawn({"cat"}).has_value());

        p.close();
        CHECK(!p.is_connected());
    }

    // A large transfer in both directions at once.
    {
        Peer p;
        CHECK(p.spawn({"cat"}).has_value());
        std::string data(2 * 1024 * 1024, '\0');
        for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i * 31);

        std::thread sender([&] { CHECK(p.send(data.data(), data.size()).has_value()); });
        std::string got(data.size(), '\0');
        CHECK(p.receive(got.data(), got.size()).has_value());
        sender.join();
        CHECK(got == data);
    }

    // Noise before the handshake is skipped.
    {
        Peer p;
        auto r = p.spawn({"sh", "-c", "echo noise; echo JSYNC1; echo data"});
        CHECK(r.has_value());
        char buf[5];
        CHECK(p.receive(buf, 5).has_value());
        CHECK(std::memcmp(buf, "data\n", 5) == 0);
        // The remote then ends the stream.
        CHECK(!p.receive(buf, 1).has_value());
    }

    // A remote that exits without handshaking is an error.
    {
        Peer p;
        auto r = p.spawn({"false"});
        CHECK(!r.has_value());
        CHECK(!p.is_connected());
    }

    // A command that does not exist is an error.
    {
        Peer p;
        CHECK(!p.spawn({"jsync-no-such-command"}).has_value());
        CHECK(!p.is_connected());
    }

    // Two peers joined by a pair of pipes (the --server shape, in-process).
    {
        int a_to_b[2], b_to_a[2];
        CHECK(::pipe(a_to_b) == 0);
        CHECK(::pipe(b_to_a) == 0);

        Peer a, b;
        ExpectBool ra = false, rb = false;
        std::thread ta([&] { ra = a.attach(b_to_a[0], a_to_b[1], true); });
        std::thread tb([&] { rb = b.attach(a_to_b[0], b_to_a[1], true); });
        ta.join();
        tb.join();
        CHECK(ra.has_value());
        CHECK(rb.has_value());

        CHECK(a.send("ping", 4).has_value());
        char buf[4];
        CHECK(b.receive(buf, 4).has_value());
        CHECK(std::memcmp(buf, "ping", 4) == 0);
        CHECK(b.send("pong", 4).has_value());
        CHECK(a.receive(buf, 4).has_value());
        CHECK(std::memcmp(buf, "pong", 4) == 0);

        // Closing one end gives the other EOF.
        a.close();
        CHECK(!b.receive(buf, 1).has_value());
    }

    if (failures == 0) std::cout << "all peer tests passed\n";
    return failures == 0 ? 0 : 1;
}
