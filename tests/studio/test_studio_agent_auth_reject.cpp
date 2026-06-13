/// @file test_studio_agent_auth_reject.cpp
/// @brief ST32 — Auth gate. Two layers:
///   1. Production attach toggle (default off in release builds without
///      THREADMAXX_STUDIO_AGENT_ENABLE_PROD=1).
///   2. Per-peer auth token: every request from an unauthenticated
///      peer is rejected (ok=0) until Authenticate(token) succeeds.

#include "Check.hpp"

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/remote_data_source.hpp>

#include <threadmaxx_network/transport.hpp>

#include <array>
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
        studio::EngineFrameSummary s{};
        s.tick = 99;
        s.systemCount = 3;
        return s;
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
    studio::RemoteDataSource remote{studioTransport,
                                    agentTransport.localPeer()};

    // -----------------------------------------------------------------
    // Layer 1: production attach toggle.
    //   Force-disable; the agent drains but does not dispatch — no
    //   response comes back even for a valid request.
    agent.setAttachEnabled(false);
    CHECK(!agent.attachEnabled());

    remote.requestEngineSnapshot();
    CHECK_EQ(agent.pump(), 0u);
    CHECK_EQ(agent.requestsHandled(), 0u);

    std::array<network::ReceivedPacket, 4> inbox{};
    CHECK_EQ(studioTransport.receive(inbox), 0u);

    // Cache stays cold.
    CHECK(!remote.engineSnapshot().has_value());

    // -----------------------------------------------------------------
    // Layer 2: per-peer auth token.
    //   Enable attach + set a token. Requests from an unauthenticated
    //   peer come back with ok=0; no engine state leaks.
    agent.setAttachEnabled(true);
    agent.setAuthToken("s3cret");
    CHECK_EQ(agent.authToken(), std::string_view{"s3cret"});
    CHECK_EQ(agent.authenticatedPeerCount(), 0u);

    remote.requestEngineSnapshot();
    CHECK_EQ(agent.pump(), 1u);  // dispatched but rejected.
    CHECK_EQ(remote.pump(), 1u);
    // EngineSnapshot response with ok=0 → cache cleared (was already
    // empty; verify still empty).
    CHECK(!remote.engineSnapshot().has_value());

    // Wrong token: AuthResult ok=0, peer remains unauthenticated.
    remote.authenticate("wrong");
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    CHECK(!remote.lastAuthAccepted());
    CHECK_EQ(remote.authResponsesReceived(), 1u);
    CHECK_EQ(agent.authenticatedPeerCount(), 0u);

    // Right token: AuthResult ok=1, peer authenticated.
    remote.authenticate("s3cret");
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    CHECK(remote.lastAuthAccepted());
    CHECK_EQ(remote.authResponsesReceived(), 2u);
    CHECK_EQ(agent.authenticatedPeerCount(), 1u);
    CHECK(agent.isPeerAuthenticated(studioTransport.localPeer()));

    // Now GetEngineSnapshot succeeds.
    remote.requestEngineSnapshot();
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    auto snap = remote.engineSnapshot();
    CHECK(snap.has_value());
    CHECK_EQ(snap->tick, 99u);
    CHECK_EQ(snap->systemCount, 3u);

    // Rotating the token drops every previously-authed peer.
    agent.setAuthToken("new-secret");
    CHECK_EQ(agent.authenticatedPeerCount(), 0u);

    remote.requestEngineSnapshot();
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    // Cache invalidated by the ok=0 response.
    CHECK(!remote.engineSnapshot().has_value());

    // Open mode: empty token = no auth required.
    agent.setAuthToken("");
    CHECK(agent.authToken().empty());
    // isPeerAuthenticated returns true for any peer in open mode.
    CHECK(agent.isPeerAuthenticated(studioTransport.localPeer()));

    remote.requestEngineSnapshot();
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    CHECK(remote.engineSnapshot().has_value());

    EXIT_WITH_RESULT();
}
