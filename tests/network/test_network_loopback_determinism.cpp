/// @file test_network_loopback_determinism.cpp
/// @brief NW2 — fixed-seed profile produces the same delivery
/// sequence across two runs.

#include "Check.hpp"

#include <threadmaxx_network/transport.hpp>

#include <array>
#include <memory>
#include <vector>

namespace {

std::vector<int> run() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    TransportProfile profile{};
    profile.lossRate = 0.3;
    profile.reorderRate = 0.2;
    profile.duplicationRate = 0.1;
    profile.rngSeed = 0xDEADBEEFull;
    LoopbackTransport sender{hub, profile};
    LoopbackTransport receiver{hub};

    constexpr int kSends = 300;
    for (int i = 0; i < kSends; ++i) {
        std::array<std::byte, 4> payload{
            static_cast<std::byte>((i >>  0) & 0xFF),
            static_cast<std::byte>((i >>  8) & 0xFF),
            static_cast<std::byte>((i >> 16) & 0xFF),
            static_cast<std::byte>((i >> 24) & 0xFF),
        };
        PacketView pv{payload.data(), payload.size()};
        sender.send(receiver.localPeer(), pv);
    }

    std::array<ReceivedPacket, 64> tmp{};
    std::vector<int> out;
    while (true) {
        const auto n = receiver.receive(tmp);
        if (n == 0) break;
        for (std::size_t i = 0; i < n; ++i) {
            const auto& p = tmp[i].payload;
            int v = static_cast<int>(p[0]) |
                    (static_cast<int>(p[1]) << 8) |
                    (static_cast<int>(p[2]) << 16) |
                    (static_cast<int>(p[3]) << 24);
            out.push_back(v);
        }
    }
    return out;
}

} // namespace

int main() {
    auto a = run();
    auto b = run();
    CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK_EQ(a[i], b[i]);
    }
    EXIT_WITH_RESULT();
}
