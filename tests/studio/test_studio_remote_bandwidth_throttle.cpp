/// @file test_studio_remote_bandwidth_throttle.cpp
/// @brief ST33 — per-tick request budget caps outbound traffic; the
/// BandwidthPanel sampling ring records the cap + drops.

#include "Check.hpp"

#include <threadmaxx_studio/agent.hpp>
#include <threadmaxx_studio/data_source.hpp>
#include <threadmaxx_studio/panels/bandwidth.hpp>
#include <threadmaxx_studio/remote_data_source.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>

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
        studio::EngineFrameSummary s{};
        s.tick = 1;
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
    agent.setAttachEnabled(true);
    studio::RemoteDataSource remote{studioTransport,
                                    agentTransport.localPeer()};

    studio::BandwidthPanel panel{remote, &agent, /*capacity*/ 4};
    CHECK_EQ(panel.sampleCount(), 0u);

    // No throttle yet — every request goes through.
    for (int i = 0; i < 5; ++i) {
        const auto id = remote.requestEngineSnapshot();
        CHECK(id != 0u);
    }
    CHECK_EQ(remote.requestsThisTick(), 5u);
    CHECK_EQ(remote.requestsDropped(), 0u);

    // Apply a budget via the panel's setter.
    panel.setRequestsPerTickBudget(2);
    CHECK_EQ(remote.requestsPerTickBudget(), 2u);

    // beginTick resets the counter; the budget caps subsequent calls.
    remote.beginTick();
    CHECK_EQ(remote.requestsThisTick(), 0u);

    const auto a = remote.requestEngineSnapshot();  // allowed (1/2)
    const auto b = remote.requestEngineSnapshot();  // allowed (2/2)
    const auto c = remote.requestEngineSnapshot();  // budget exhausted
    CHECK(a != 0u);
    CHECK(b != 0u);
    CHECK_EQ(c, 0u);
    CHECK_EQ(remote.requestsThisTick(), 2u);
    CHECK_EQ(remote.requestsDropped(), 1u);

    // submitCommand also charges the budget. (Already at cap.)
    CHECK(!remote.submitCommand("Translate"));
    CHECK_EQ(remote.requestsDropped(), 2u);

    // Sample at the end of the tick — panel records the load.
    panel.sample(/*tick*/ 42);
    CHECK_EQ(panel.sampleCount(), 1u);
    const auto sample = panel.latest();
    CHECK_EQ(sample.tick, 42u);
    CHECK_EQ(sample.requestsThisTick, 2u);
    CHECK_EQ(sample.requestsDropped, 2u);

    // Next tick: budget room again.
    remote.beginTick();
    const auto d = remote.requestEngineSnapshot();
    CHECK(d != 0u);
    CHECK_EQ(remote.requestsThisTick(), 1u);

    // Pump everything so the panel sample captures non-zero rx.
    CHECK(agent.pump() > 0u);
    CHECK(remote.pump() > 0u);
    panel.sample(/*tick*/ 43);
    CHECK_EQ(panel.sampleCount(), 2u);
    const auto sample2 = panel.latest();
    CHECK(sample2.remoteBytesReceived > 0u);
    CHECK(sample2.agentBytesSent > 0u);

    // Capacity = 4: drive samples past capacity, ring drops oldest.
    panel.sample(44);
    panel.sample(45);
    panel.sample(46);   // capacity reached
    panel.sample(47);   // drops tick 42
    CHECK_EQ(panel.sampleCount(), 4u);

    // Smoke-render via headless backend to exercise the draw path.
    editor::HeadlessBackend headless;
    CHECK(headless.initialize());
    headless.beginFrame();
    panel.render(headless, source);
    CHECK(panel.rowCount() > 0u);

    EXIT_WITH_RESULT();
}
