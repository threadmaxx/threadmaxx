/// @file test_network_session_unique.cpp
/// @brief NW3 — two concurrent clients get different SessionIds.

#include "Check.hpp"

#include <threadmaxx_network/session.hpp>
#include <threadmaxx_network/transport.hpp>

#include <memory>

int main() {
    using namespace threadmaxx::network;

    auto hub = std::make_shared<LoopbackHub>();
    LoopbackTransport serverTransport{hub};
    LoopbackTransport c1{hub};
    LoopbackTransport c2{hub};

    ServerSession server{&serverTransport};
    ClientSession client1{&c1, serverTransport.localPeer()};
    ClientSession client2{&c2, serverTransport.localPeer()};

    CHECK(client1.beginHandshake(0x1111));
    CHECK(client2.beginHandshake(0x2222));
    server.pumpReceive();
    server.pumpReceive();
    client1.pumpReceive();
    client2.pumpReceive();

    CHECK(client1.connected());
    CHECK(client2.connected());
    CHECK(client1.sessionId() != client2.sessionId());
    CHECK_EQ(server.connectedPeerCount(), 2u);

    EXIT_WITH_RESULT();
}
