/// @file test_network_loopback_send_receive.cpp
/// @brief NW2 — peer A sends, peer B receives the same bytes via
/// the loopback hub.

#include "Check.hpp"

#include <threadmaxx_network/transport.hpp>

#include <array>
#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    LoopbackTransport a{hub};
    LoopbackTransport b{hub};
    CHECK(a.localPeer() != b.localPeer());

    const std::array<std::byte, 5> payload{
        std::byte{'h'}, std::byte{'e'}, std::byte{'l'},
        std::byte{'l'}, std::byte{'o'},
    };
    PacketView pv{payload.data(), payload.size()};
    CHECK(a.send(b.localPeer(), pv));

    std::array<ReceivedPacket, 4> inbox{};
    const auto n = b.receive(inbox);
    CHECK_EQ(n, 1u);
    CHECK(inbox[0].peer == a.localPeer());
    CHECK_EQ(inbox[0].payload.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(inbox[0].payload[i] == payload[i]);
    }

    // After the drain the inbox is empty.
    CHECK_EQ(b.receive(inbox), 0u);

    EXIT_WITH_RESULT();
}
