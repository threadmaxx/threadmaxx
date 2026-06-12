/// @file test_network_input_ack.cpp
/// @brief NW4 — server acks tick 110; client drops ticks ≤110 from
/// its retransmit queue.

#include "Check.hpp"

#include <threadmaxx_network/session.hpp>
#include <threadmaxx_network/transport.hpp>

#include <array>
#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    LoopbackTransport st{hub};
    LoopbackTransport ct{hub};
    ServerSession server{&st};
    ClientSession client{&ct, st.localPeer()};

    CHECK(client.beginHandshake());
    server.pumpReceive();
    client.pumpReceive();

    const std::array<std::byte, 1> payload{std::byte{0xAB}};
    for (std::uint32_t t = 100; t <= 120; ++t) {
        client.sendInput(TickId{t}, payload);
    }
    CHECK_EQ(client.pendingInputCount(), 21u);
    server.pumpReceive();

    // Server releases inputs up to and including tick 110.
    server.releaseInputsUpTo(ct.localPeer(), TickId{110});
    CHECK_EQ(server.inputsFor(ct.localPeer()).size(), 10u);

    client.pumpReceive();
    CHECK_EQ(client.lastAcked().value, 110u);
    CHECK_EQ(client.pendingInputCount(), 10u);

    EXIT_WITH_RESULT();
}
