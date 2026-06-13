/// @file test_editor_profiler_aggregate.cpp
/// @brief E13 — feeding three FrameSnapshots produces an aggregate
/// where per-system rows are sorted by avgUpdateSeconds descending and
/// the tick / step rollups match the fed values.

#include "Check.hpp"

#include <threadmaxx_editor/profiler.hpp>

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/Trace.hpp>

#include <array>

namespace {

threadmaxx::FrameSnapshot makeSnap(std::uint64_t tick, double stepSeconds,
                                   std::span<const threadmaxx::SystemStats> sys,
                                   threadmaxx::JobSystemStats jobs = {}) {
    threadmaxx::FrameSnapshot snap;
    snap.engine.tick            = tick;
    snap.engine.lastStepSeconds = stepSeconds;
    snap.systems                = sys;
    snap.jobs                   = jobs;
    return snap;
}

} // namespace

int main() {
    using namespace threadmaxx;
    editor::ProfilerView view{8};

    SystemStats slow{};
    slow.name              = "SlowSystem";
    slow.lastUpdateSeconds = 0.010;
    slow.waitSeconds       = 0.002;
    slow.peakQueueDepth    = 7;

    SystemStats fast{};
    fast.name              = "FastSystem";
    fast.lastUpdateSeconds = 0.001;
    fast.waitSeconds       = 0.000;
    fast.peakQueueDepth    = 1;

    const std::array<SystemStats, 2> tick1{slow, fast};
    view.onFrame(makeSnap(101, 0.020, tick1));

    SystemStats slow2 = slow;
    slow2.lastUpdateSeconds = 0.014;
    slow2.peakQueueDepth    = 9;
    const std::array<SystemStats, 2> tick2{slow2, fast};
    view.onFrame(makeSnap(102, 0.024, tick2));

    SystemStats fast3 = fast;
    fast3.lastUpdateSeconds = 0.003;
    const std::array<SystemStats, 2> tick3{slow, fast3};
    view.onFrame(makeSnap(103, 0.018, tick3));

    CHECK_EQ(view.sampleCount(), 3u);
    const auto s = view.summary();
    CHECK_EQ(s.samples, 3u);
    CHECK_EQ(s.firstTick, 101u);
    CHECK_EQ(s.lastTick, 103u);
    // (0.020 + 0.024 + 0.018) / 3 = ~0.02067
    CHECK(s.avgStepSeconds > 0.020);
    CHECK(s.avgStepSeconds < 0.021);
    CHECK(s.minStepSeconds <= 0.018 + 1e-9);
    CHECK(s.maxStepSeconds >= 0.024 - 1e-9);

    CHECK_EQ(s.systems.size(), 2u);
    // Sort: slow first.
    CHECK(s.systems[0].name == "SlowSystem");
    CHECK(s.systems[1].name == "FastSystem");
    CHECK(s.systems[0].sampleCount == 3u);
    CHECK(s.systems[1].sampleCount == 3u);
    CHECK(s.systems[0].peakQueueDepth == 9u);
    CHECK(s.systems[0].maxUpdateSeconds >= 0.014 - 1e-9);
    // total = 0.010 + 0.014 + 0.010; avg = total/3
    CHECK(s.systems[0].totalUpdateSeconds > 0.033);
    CHECK(s.systems[0].avgUpdateSeconds > 0.011);
    CHECK(s.systems[0].totalWaitSeconds > 0.005);

    EXIT_WITH_RESULT();
}
