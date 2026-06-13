/// @file test_studio_remote_uses_interest_filter.cpp
/// @brief ST33 — pinned contract: the remote IStudioDataSource MUST
/// opt into `interest::ClientFocus` like any other peer. No
/// special-cased full-world reads. Test verifies the focus pushes
/// over the wire and the agent stores it keyed by the actual sending
/// peer (not the wire-supplied peerId, which could be forged).

#include "Check.hpp"

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/remote_data_source.hpp>

#include <threadmaxx_network/interest.hpp>
#include <threadmaxx_network/transport.hpp>

#include <memory>
#include <optional>

namespace {

using namespace threadmaxx;

class StubDataSource final : public studio::IStudioDataSource {
public:
    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }
    std::optional<studio::EngineFrameSummary>
    engineSnapshot() const override {
        return std::nullopt;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    StubDataSource source;
    auto hub = std::make_shared<network::LoopbackHub>();
    network::LoopbackTransport agentTransport{hub};
    network::LoopbackTransport studioTransport{hub};

    studio::StudioAgent agent{agentTransport, source};
    agent.setAttachEnabled(true);
    studio::RemoteDataSource remote{studioTransport,
                                    agentTransport.localPeer()};

    // Initially no focus on either side.
    CHECK(remote.clientFocus() == nullptr);
    CHECK_EQ(agent.peerFocusCount(), 0u);
    CHECK(agent.peerFocus(studioTransport.localPeer()) == nullptr);

    // No-focus push is a no-op (returns 0).
    CHECK_EQ(remote.pushClientFocus(), 0u);

    // Configure the focus on the studio side.
    network::ClientFocus focus{};
    focus.peer  = studioTransport.localPeer();
    focus.x     = 12.0f;
    focus.y     = 3.0f;
    focus.z     = -7.5f;
    focus.config.radius = 64.0f;
    remote.setClientFocus(focus);
    CHECK(remote.clientFocus() != nullptr);
    CHECK_EQ(remote.clientFocus()->config.radius, 64.0f);

    // Push it. Pump on both sides.
    const auto reqId = remote.pushClientFocus();
    CHECK(reqId != 0u);
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    CHECK(remote.lastFocusAccepted());
    CHECK_EQ(remote.focusResponsesReceived(), 1u);

    // Agent recorded the focus keyed by the actual sending peer.
    CHECK_EQ(agent.peerFocusCount(), 1u);
    const auto* stored = agent.peerFocus(studioTransport.localPeer());
    CHECK(stored != nullptr);
    CHECK_EQ(stored->x, 12.0f);
    CHECK_EQ(stored->y, 3.0f);
    CHECK_EQ(stored->z, -7.5f);
    CHECK_EQ(stored->config.radius, 64.0f);
    // The wire-supplied peerId is overridden by the actual sender —
    // a forged wire field can't poison another peer's focus.
    CHECK(stored->peer == studioTransport.localPeer());

    // Forge the wire peerId to another value; agent still keys by the
    // real sender.
    network::ClientFocus forged{};
    forged.peer  = network::PeerId{99999u};  // not the studio's peer
    forged.x     = 1.0f;
    forged.y     = 2.0f;
    forged.z     = 3.0f;
    forged.config.radius = 5.0f;
    remote.setClientFocus(forged);
    const auto reqId2 = remote.pushClientFocus();
    CHECK(reqId2 != 0u);
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    CHECK(remote.lastFocusAccepted());

    // Still one entry, still keyed by the real studio peer. The
    // forged peer 99999 has NO focus recorded.
    CHECK_EQ(agent.peerFocusCount(), 1u);
    CHECK(agent.peerFocus(network::PeerId{99999u}) == nullptr);
    const auto* updated = agent.peerFocus(studioTransport.localPeer());
    CHECK(updated != nullptr);
    CHECK_EQ(updated->x, 1.0f);
    CHECK(updated->peer == studioTransport.localPeer());

    EXIT_WITH_RESULT();
}
