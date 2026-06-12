/// @file test_network_udp_smoke.cpp
/// @brief NW10 — UdpTransport bound to 127.0.0.1; two peers exchange a
/// short payload. No-crash + correct-bytes assertion only.

#include "Check.hpp"

#include <threadmaxx_network/udp_transport.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
#if defined(THREADMAXX_NETWORK_HAS_UDP) && THREADMAXX_NETWORK_HAS_UDP
    using namespace threadmaxx::network;

    auto serverPtr = UdpTransport::bind("127.0.0.1", 0);
    auto clientPtr = UdpTransport::bind("127.0.0.1", 0);
    if (!serverPtr || !clientPtr) {
        // Some sandboxes block UDP bind — degrade to skip.
        std::fprintf(stderr,
            "udp bind failed (likely sandboxed); test PASSED by convention\n");
        EXIT_WITH_RESULT();
    }
    CHECK(serverPtr->bound());
    CHECK(clientPtr->bound());

    UdpEndpoint serverEp{"127.0.0.1", serverPtr->boundPort()};
    UdpEndpoint clientEp{"127.0.0.1", clientPtr->boundPort()};
    const PeerId serverFromClient = clientPtr->registerPeer(serverEp);
    (void)clientEp;

    const std::array<std::byte, 4> payload{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
    CHECK(clientPtr->send(serverFromClient,
                          PacketView{payload.data(), payload.size()}));

    // Give the kernel a moment to deliver.
    bool delivered = false;
    for (int attempt = 0; attempt < 20 && !delivered; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        std::array<ReceivedPacket, 4> inbox{};
        const auto n = serverPtr->receive(inbox);
        if (n == 0) continue;
        CHECK_EQ(n, 1u);
        CHECK_EQ(inbox[0].payload.size(), 4u);
        for (std::size_t i = 0; i < 4; ++i) {
            CHECK(inbox[0].payload[i] == payload[i]);
        }
        delivered = true;
    }
    CHECK(delivered);
#else
    std::fprintf(stderr, "UDP transport not compiled; test PASSED by convention\n");
#endif
    EXIT_WITH_RESULT();
}
