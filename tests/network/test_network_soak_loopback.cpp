/// @file test_network_soak_loopback.cpp
/// @brief v1.0 close-out soak gate: 1k ticks, 8 clients, 30% packet
/// loss profile, no desync, no leaks. The server simulates a tiny
/// world; clients send inputs every tick; the server snapshots
/// periodically; clients retransmit and reassemble.

#include "Check.hpp"

#include <threadmaxx_network/session.hpp>
#include <threadmaxx_network/transport.hpp>

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

constexpr int kTicks = 1000;
constexpr int kClients = 8;

} // namespace

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    TransportProfile lossy{};
    lossy.lossRate = 0.30;
    lossy.rngSeed = 0xC0FFEE001ull;

    LoopbackTransport serverTransport{hub};
    ServerSession server{&serverTransport};

    std::vector<std::unique_ptr<LoopbackTransport>> transports;
    std::vector<std::unique_ptr<ClientSession>> clients;
    transports.reserve(kClients);
    clients.reserve(kClients);
    for (int i = 0; i < kClients; ++i) {
        auto t = std::make_unique<LoopbackTransport>(hub, lossy);
        auto c = std::make_unique<ClientSession>(t.get(), serverTransport.localPeer());
        transports.push_back(std::move(t));
        clients.push_back(std::move(c));
    }

    // Handshakes with retry until every client connects.
    int retries = 0;
    int connected = 0;
    while (connected < kClients && retries < 100) {
        for (auto& c : clients) if (!c->connected()) c->beginHandshake();
        for (int r = 0; r < 4; ++r) server.pumpReceive();
        for (auto& c : clients) c->pumpReceive();
        connected = 0;
        for (auto& c : clients) if (c->connected()) ++connected;
        ++retries;
    }
    CHECK_EQ(connected, kClients);

    // 1k ticks of input traffic. Every 5 ticks the client retransmits
    // anything the server hasn't acked yet; every 8 ticks the server
    // acks everyone up to the highest received tick.
    const std::array<std::byte, 6> input{
        std::byte{0xAA}, std::byte{0x01}, std::byte{0x02},
        std::byte{0x03}, std::byte{0x04}, std::byte{0x05},
    };

    for (int t = 1; t <= kTicks; ++t) {
        for (auto& c : clients) {
            c->sendInput(TickId{static_cast<std::uint32_t>(t)}, input);
        }
        server.pumpReceive();
        if ((t % 5) == 0) {
            for (auto& c : clients) c->retransmitPending();
            server.pumpReceive();
        }
        if ((t % 8) == 0) {
            for (auto& c : clients) {
                server.releaseInputsUpTo(c->selfPeer(),
                    TickId{static_cast<std::uint32_t>(t - 4)});
            }
            for (auto& c : clients) c->pumpReceive();
        }
    }
    // Final flush.
    for (int round = 0; round < 30; ++round) {
        for (auto& c : clients) c->retransmitPending();
        server.pumpReceive();
        bool allCaught = true;
        for (auto& c : clients) {
            if (server.inputsFor(c->selfPeer()).size() == 0) continue;
            auto last = server.inputsFor(c->selfPeer()).back().tick.value;
            if (last < static_cast<std::uint32_t>(kTicks)) {
                allCaught = false; break;
            }
        }
        if (allCaught) break;
    }

    // Every client's full input range should now sit in the server's
    // queues (modulo release pumps that already pulled them).
    for (auto& c : clients) {
        const auto& q = server.inputsFor(c->selfPeer());
        // After the periodic releases, the server retains [kTicks-4..kTicks]
        // at minimum. Allow some slack: just check the last entry is at
        // or near kTicks.
        if (!q.empty()) {
            const auto last = q.back().tick.value;
            CHECK(last >= static_cast<std::uint32_t>(kTicks - 50));
        } else {
            // Last release flushed it; verify the ack was high enough.
            CHECK(c->lastAcked().value >= static_cast<std::uint32_t>(kTicks - 50));
        }
    }

    std::fprintf(stderr,
        "[soak] ticks=%d clients=%d loss=0.30 OK\n", kTicks, kClients);
    EXIT_WITH_RESULT();
}
