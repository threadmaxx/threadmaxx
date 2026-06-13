/// @file panels/NetworkSessionPanel.cpp
/// @brief ST24 — `NetworkSessionPanel` implementation.

#include <threadmaxx_studio/panels/network_session.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_network/diagnostics.hpp>
#include <threadmaxx_network/session.hpp>

#include <cstdio>

namespace threadmaxx::studio {

NetworkSessionPanel::NetworkSessionPanel(
    const network::ServerSession& session) noexcept
    : session_(&session) {}

void NetworkSessionPanel::render(editor::IEditorBackend& backend,
                                 IStudioDataSource&) {
    if (session_ == nullptr) {
        backend.drawText("Network: <detached>", 0.0f, 0.0f);
        lastRows_ = 0;
        return;
    }

    const auto peers = session_->listPeers();
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Network  peers=%zu  connected=%zu",
                  peers.size(), session_->connectedPeerCount());
    backend.drawText(buf, 0.0f, 0.0f);

    lastRows_ = peers.size();
    float y = 16.0f;
    for (const auto& p : peers) {
        std::snprintf(buf, sizeof(buf),
                      "peer#%u  session=%llu  %-3s  rseq=%u  ack=0x%08X  "
                      "lseq=%u  in=%u",
                      p.peer.value,
                      static_cast<unsigned long long>(p.session.value),
                      p.connected ? "ON" : "OFF",
                      p.remoteSeq, p.remoteAckBits, p.localSeq,
                      p.pendingInputCount);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }
}

} // namespace threadmaxx::studio
