/// @file test_network_input_delivery.cpp
/// @brief NW4 — client submits input for ticks 100..120; server's
/// input queue contains all 21 entries in tick order.

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
    CHECK(client.connected());

    const std::array<std::byte, 4> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    for (std::uint32_t t = 100; t <= 120; ++t) {
        CHECK(client.sendInput(TickId{t}, payload));
    }
    server.pumpReceive();

    const auto rows = server.inputsFor(ct.localPeer());
    CHECK_EQ(rows.size(), 21u);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        CHECK_EQ(rows[i].tick.value, 100u + static_cast<std::uint32_t>(i));
        CHECK_EQ(rows[i].bytes.size(), 4u);
    }

    EXIT_WITH_RESULT();
}
