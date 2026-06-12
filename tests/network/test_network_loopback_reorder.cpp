/// @file test_network_loopback_reorder.cpp
/// @brief NW2 — reorder profile produces out-of-order delivery while
/// preserving payload integrity.

#include "Check.hpp"

#include <threadmaxx_network/transport.hpp>

#include <array>
#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    TransportProfile profile{};
    profile.reorderRate = 0.5;
    profile.rngSeed = 999;
    LoopbackTransport sender{hub, profile};
    LoopbackTransport receiver{hub};

    constexpr int kSends = 200;
    for (int i = 0; i < kSends; ++i) {
        std::array<std::byte, 4> payload{
            static_cast<std::byte>((i >>  0) & 0xFF),
            static_cast<std::byte>((i >>  8) & 0xFF),
            static_cast<std::byte>((i >> 16) & 0xFF),
            static_cast<std::byte>((i >> 24) & 0xFF),
        };
        PacketView pv{payload.data(), payload.size()};
        CHECK(sender.send(receiver.localPeer(), pv));
    }

    std::array<ReceivedPacket, 64> tmp{};
    std::vector<int> seq;
    seq.reserve(kSends);
    while (true) {
        const auto n = receiver.receive(tmp);
        if (n == 0) break;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& p = tmp[i].payload;
            int v = static_cast<int>(p[0]) |
                    (static_cast<int>(p[1]) << 8) |
                    (static_cast<int>(p[2]) << 16) |
                    (static_cast<int>(p[3]) << 24);
            seq.push_back(v);
        }
    }
    CHECK_EQ(seq.size(), static_cast<std::size_t>(kSends));

    // At least one out-of-order pair must exist when reorderRate>0
    // and we've delivered 200 entries.
    int outOfOrder = 0;
    for (std::size_t i = 1; i < seq.size(); ++i) {
        if (seq[i] < seq[i - 1]) ++outOfOrder;
    }
    CHECK(outOfOrder > 0);

    EXIT_WITH_RESULT();
}
