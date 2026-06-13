/// @file test_studio_network_peer_list.cpp
/// @brief ST24 — bind a ServerSession with a connected client;
/// `peerRowCount()` matches `listPeers().size()`, render emits
/// header + one row per peer.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/network_session.hpp>

#include <threadmaxx_network/session.hpp>
#include <threadmaxx_network/transport.hpp>

#include <memory>

namespace {

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::NetworkSessionPanel panel;

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.peerRowCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Wire a server + client over the loopback hub.
    auto hub = std::make_shared<network::LoopbackHub>();
    network::LoopbackTransport st{hub};
    network::LoopbackTransport ct{hub};
    network::ServerSession server{&st};
    network::ClientSession client{&ct, st.localPeer()};

    // Empty server.
    panel.setSession(&server);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.peerRowCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u); // header only

    // Handshake — one connected peer expected.
    for (int retry = 0; retry < 5; ++retry) {
        client.beginHandshake();
        server.pumpReceive();
        client.pumpReceive();
        if (client.connected()) break;
    }
    CHECK(client.connected());

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.peerRowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 2u); // header + 1 row

    backend.shutdown();
    EXIT_WITH_RESULT();
}
