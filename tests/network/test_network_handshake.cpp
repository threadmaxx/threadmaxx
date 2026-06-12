/// @file test_network_handshake.cpp
/// @brief NW3 — client sends Hello, server replies Welcome with a
/// SessionId; both sides reach connected state.

#include "Check.hpp"

#include <threadmaxx_network/session.hpp>
#include <threadmaxx_network/transport.hpp>

#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    LoopbackTransport serverTransport{hub};
    LoopbackTransport clientTransport{hub};

    ServerSession server{&serverTransport};
    ClientSession client{&clientTransport, serverTransport.localPeer()};

    CHECK(client.beginHandshake(0xCAFEull));
    CHECK_EQ(server.pumpReceive(), 1u);
    CHECK_EQ(server.connectedPeerCount(), 1u);

    CHECK_EQ(client.pumpReceive(), 1u);
    CHECK(client.connected());
    CHECK(client.sessionId().valid());

    const auto* peerState = server.peer(clientTransport.localPeer());
    CHECK(peerState != nullptr);
    CHECK_EQ(peerState->session.value, client.sessionId().value);

    EXIT_WITH_RESULT();
}
