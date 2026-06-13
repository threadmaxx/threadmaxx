/// @file test_network_peer_summary.cpp
/// @brief NW11 — `ServerSession::listPeers` returns one `PeerSummary`
/// per known peer with connected / sequence / input-queue depth state.

#include "Check.hpp"

#include <threadmaxx_network/diagnostics.hpp>
#include <threadmaxx_network/session.hpp>
#include <threadmaxx_network/transport.hpp>

#include <array>
#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    LoopbackTransport serverTransport{hub};
    ServerSession server{&serverTransport};

    // No peers initially.
    {
        const auto peers = server.listPeers();
        CHECK_EQ(peers.size(), 0u);
    }

    // Spin up two clients + handshake.
    constexpr int kClients = 2;
    std::vector<std::unique_ptr<LoopbackTransport>> transports;
    std::vector<std::unique_ptr<ClientSession>> clients;
    for (int i = 0; i < kClients; ++i) {
        auto t = std::make_unique<LoopbackTransport>(hub);
        auto c = std::make_unique<ClientSession>(t.get(),
                                                 serverTransport.localPeer());
        transports.push_back(std::move(t));
        clients.push_back(std::move(c));
    }

    int retries = 0;
    int connected = 0;
    while (connected < kClients && retries < 50) {
        for (auto& c : clients) if (!c->connected()) c->beginHandshake();
        for (int r = 0; r < 4; ++r) server.pumpReceive();
        for (auto& c : clients) c->pumpReceive();
        connected = 0;
        for (auto& c : clients) if (c->connected()) ++connected;
        ++retries;
    }
    CHECK_EQ(connected, kClients);

    // After connection both peers appear in the listing as connected.
    {
        const auto peers = server.listPeers();
        CHECK_EQ(peers.size(), 2u);
        int connectedCount = 0;
        for (const auto& s : peers) {
            if (s.connected) ++connectedCount;
            CHECK_EQ(s.pendingInputCount, 0u);
        }
        CHECK_EQ(connectedCount, kClients);
    }

    // Send one input from client 0 and let the server drain it.
    const std::array<std::byte, 4> input{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
    };
    clients[0]->sendInput(TickId{1}, input);
    server.pumpReceive();

    {
        const auto peers = server.listPeers();
        std::uint32_t queued = 0;
        for (const auto& s : peers) {
            if (s.peer == clients[0]->selfPeer()) {
                queued = s.pendingInputCount;
            }
        }
        CHECK_EQ(queued, 1u);
    }

    EXIT_WITH_RESULT();
}
