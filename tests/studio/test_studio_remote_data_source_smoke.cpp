/// @file test_studio_remote_data_source_smoke.cpp
/// @brief ST30 — full agent + RemoteDataSource round-trip over the
/// LoopbackHub. Verifies cold-cache graceful degradation, request
/// issuance, response decode + cache update, and invalidate.

#include "Check.hpp"

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/remote_data_source.hpp>

#include <threadmaxx_network/transport.hpp>

#include <memory>
#include <optional>

namespace {

using namespace threadmaxx;

class StubDataSource final : public studio::IStudioDataSource {
public:
    explicit StubDataSource(studio::EngineFrameSummary s) : summary_(s) {}
    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }
    std::optional<studio::EngineFrameSummary>
    engineSnapshot() const override {
        return summary_;
    }
    void setSummary(studio::EngineFrameSummary s) { summary_ = s; }
private:
    studio::EngineFrameSummary summary_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    studio::EngineFrameSummary expected{};
    expected.tick = 4242;
    expected.lastStepSeconds = 0.0099;
    expected.paused = false;
    expected.systemCount = 11;
    expected.workerCount = 8;

    StubDataSource source{expected};

    auto hub = std::make_shared<network::LoopbackHub>();
    network::LoopbackTransport agentTransport{hub};
    network::LoopbackTransport studioTransport{hub};

    studio::StudioAgent agent{agentTransport, source};
    agent.setAttachEnabled(true);  // ST32 — production gate, on for tests.
    studio::RemoteDataSource remote{studioTransport,
                                    agentTransport.localPeer()};

    // Mode reports Remote.
    CHECK(remote.mode() == studio::AttachMode::Remote);

    // Cold cache: nullopt. Panels MUST handle this.
    CHECK(!remote.engineSnapshot().has_value());

    // Studio side issues a request.
    const auto reqId = remote.requestEngineSnapshot();
    CHECK(reqId != 0u);
    CHECK_EQ(remote.lastEngineSnapshotRequestId(), reqId);

    // Agent pumps + replies.
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(agent.requestsHandled(), 1u);

    // Studio side drains the response into its cache.
    CHECK_EQ(remote.pump(), 1u);
    CHECK_EQ(remote.responsesReceived(), 1u);
    CHECK(remote.bytesReceived() > 0u);

    auto cached = remote.engineSnapshot();
    CHECK(cached.has_value());
    CHECK_EQ(cached->tick, expected.tick);
    CHECK_EQ(cached->systemCount, expected.systemCount);
    CHECK_EQ(cached->workerCount, expected.workerCount);
    CHECK(cached->paused == expected.paused);
    CHECK(cached->lastStepSeconds == expected.lastStepSeconds);

    // Second pump finds nothing; cache stays warm.
    CHECK_EQ(remote.pump(), 0u);
    CHECK(remote.engineSnapshot().has_value());

    // Refresh: bump source, re-request, re-pump.
    studio::EngineFrameSummary updated = expected;
    updated.tick = 5000;
    updated.systemCount = 13;
    source.setSummary(updated);

    const auto reqId2 = remote.requestEngineSnapshot();
    CHECK(reqId2 != reqId);
    CHECK_EQ(agent.pump(), 1u);
    CHECK_EQ(remote.pump(), 1u);
    auto refreshed = remote.engineSnapshot();
    CHECK(refreshed.has_value());
    CHECK_EQ(refreshed->tick, 5000u);
    CHECK_EQ(refreshed->systemCount, 13u);

    // invalidateCache drops the cached value.
    remote.invalidateCache();
    CHECK(!remote.engineSnapshot().has_value());

    EXIT_WITH_RESULT();
}
