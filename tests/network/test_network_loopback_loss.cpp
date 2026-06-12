/// @file test_network_loopback_loss.cpp
/// @brief NW2 — TransportProfile{ lossRate = 0.5 } drops ~50% of
/// packets across 1000 sends (binomial within ±10%, deterministic
/// for the fixed seed).

#include "Check.hpp"

#include <threadmaxx_network/transport.hpp>

#include <array>
#include <cstdint>
#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    TransportProfile profile{};
    profile.lossRate = 0.5;
    profile.rngSeed = 12345;
    LoopbackTransport sender{hub, profile};
    LoopbackTransport receiver{hub};

    const std::array<std::byte, 4> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    PacketView pv{payload.data(), payload.size()};

    constexpr int kSends = 1000;
    for (int i = 0; i < kSends; ++i) {
        CHECK(sender.send(receiver.localPeer(), pv));
    }

    int delivered = 0;
    std::array<ReceivedPacket, 32> tmp{};
    while (true) {
        const auto n = receiver.receive(tmp);
        if (n == 0) break;
        delivered += static_cast<int>(n);
    }
    // Expected ~500 ± 50 (allow ±10% for the binomial spread).
    CHECK(delivered >= 400);
    CHECK(delivered <= 600);

    EXIT_WITH_RESULT();
}
