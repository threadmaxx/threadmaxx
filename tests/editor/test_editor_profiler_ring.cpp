/// @file test_editor_profiler_ring.cpp
/// @brief E13 — when the ring overflows, the oldest sample is dropped
/// and `firstTick`/`lastTick` reflect the surviving window.

#include "Check.hpp"

#include <threadmaxx_editor/profiler.hpp>

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/Trace.hpp>

#include <span>

int main() {
    using namespace threadmaxx;
    editor::ProfilerView view{3};
    CHECK_EQ(view.capacity(), 3u);

    SystemStats sys{};
    sys.name              = "S";
    sys.lastUpdateSeconds = 0.001;
    sys.peakQueueDepth    = 1;
    const std::span<const SystemStats> single(&sys, 1);

    for (std::uint64_t t = 1; t <= 5; ++t) {
        FrameSnapshot snap;
        snap.engine.tick            = t;
        snap.engine.lastStepSeconds = 0.01 * static_cast<double>(t);
        snap.systems                = single;
        view.onFrame(snap);
    }

    CHECK_EQ(view.sampleCount(), 3u);
    const auto s = view.summary();
    CHECK_EQ(s.samples, 3u);
    CHECK_EQ(s.firstTick, 3u);
    CHECK_EQ(s.lastTick, 5u);

    // setCapacity below current count drops oldest first.
    view.setCapacity(2);
    CHECK_EQ(view.capacity(), 2u);
    CHECK_EQ(view.sampleCount(), 2u);
    const auto s2 = view.summary();
    CHECK_EQ(s2.firstTick, 4u);
    CHECK_EQ(s2.lastTick, 5u);

    // setCapacity(0) is rejected (clamped to 1) — the most recent
    // sample survives.
    view.setCapacity(0);
    CHECK_EQ(view.capacity(), 1u);
    CHECK_EQ(view.sampleCount(), 1u);
    CHECK_EQ(view.summary().lastTick, 5u);

    view.clear();
    CHECK_EQ(view.sampleCount(), 0u);
    CHECK_EQ(view.summary().samples, 0u);

    EXIT_WITH_RESULT();
}
