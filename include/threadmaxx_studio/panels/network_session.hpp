#pragma once

/// @file panels/network_session.hpp
/// @brief ST24 — `NetworkSessionPanel` enumerates a
/// `network::ServerSession`'s connected peers via NW11's
/// `listPeers()`. Header + one row per peer (PeerId, SessionId,
/// connected flag, seq / ack stats, pending input count).

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::network {
class ServerSession;
} // namespace threadmaxx::network

namespace threadmaxx::studio {

class NetworkSessionPanel : public IStudioPanel {
public:
    NetworkSessionPanel() noexcept = default;
    explicit NetworkSessionPanel(const network::ServerSession& session) noexcept;

    void setSession(const network::ServerSession* session) noexcept {
        session_ = session;
    }
    [[nodiscard]] const network::ServerSession* session() const noexcept {
        return session_;
    }

    std::string_view id() const noexcept override {
        return "sibling.network_session";
    }
    std::string_view title() const noexcept override {
        return "Network Session";
    }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t peerRowCount() const noexcept { return lastRows_; }

private:
    const network::ServerSession* session_{nullptr};
    std::size_t                   lastRows_{0};
};

} // namespace threadmaxx::studio
