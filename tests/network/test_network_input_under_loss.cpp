/// @file test_network_input_under_loss.cpp
/// @brief NW4 — lossRate = 0.3 profile; input still arrives via
/// retransmits within the configured window.

#include "Check.hpp"

#include <threadmaxx_network/session.hpp>
#include <threadmaxx_network/transport.hpp>

#include <array>
#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    TransportProfile lossy{};
    lossy.lossRate = 0.3;
    lossy.rngSeed = 0xBADD00Dull;

    LoopbackTransport st{hub};            // server side: clean
    LoopbackTransport ct{hub, lossy};     // client side: lossy
    ServerSession server{&st};
    ClientSession client{&ct, st.localPeer()};

    // Handshake — retry on lossy first attempt.
    for (int retry = 0; retry < 5; ++retry) {
        client.beginHandshake();
        server.pumpReceive();
        client.pumpReceive();
        if (client.connected()) break;
    }
    CHECK(client.connected());

    const std::array<std::byte, 4> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};

    // 30 ticks of input, with up to 8 retransmit rounds.
    for (std::uint32_t t = 1; t <= 30; ++t) {
        client.sendInput(TickId{t}, payload);
    }
    for (int round = 0; round < 8; ++round) {
        server.pumpReceive();
        if (server.inputsFor(ct.localPeer()).size() == 30) break;
        client.retransmitPending();
    }
    CHECK_EQ(server.inputsFor(ct.localPeer()).size(), 30u);

    EXIT_WITH_RESULT();
}
