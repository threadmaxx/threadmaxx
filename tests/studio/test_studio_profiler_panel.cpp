/// @file test_studio_profiler_panel.cpp
/// @brief ST11 — feed FrameSnapshots into `ProfilerPanel::onFrame`,
/// verify the panel surfaces the same summary as the underlying
/// `editor::ProfilerView`, and that `render()` emits one header row
/// plus one row per system up to `maxRows()`.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/profiler.hpp>

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/Trace.hpp>

#include <array>

namespace {

threadmaxx::FrameSnapshot makeSnap(std::uint64_t tick, double stepSeconds,
                                   std::span<const threadmaxx::SystemStats> sys) {
    threadmaxx::FrameSnapshot snap;
    snap.engine.tick            = tick;
    snap.engine.lastStepSeconds = stepSeconds;
    snap.systems                = sys;
    return snap;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

} // namespace

int main() {
    using namespace threadmaxx;
    studio::ProfilerPanel panel{16};

    // Empty panel — render emits only the header row.
    {
        editor::HeadlessBackend backend;
        backend.initialize();
        NullSource source;
        backend.beginFrame();
        panel.render(backend, source);
        backend.endFrame();
        CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);
        backend.shutdown();
    }

    SystemStats slow{};
    slow.name              = "Slow";
    slow.lastUpdateSeconds = 0.010;
    slow.peakQueueDepth    = 3;

    SystemStats fast{};
    fast.name              = "Fast";
    fast.lastUpdateSeconds = 0.001;

    SystemStats mid{};
    mid.name              = "Mid";
    mid.lastUpdateSeconds = 0.005;

    const std::array<SystemStats, 3> sys{slow, mid, fast};
    panel.onFrame(makeSnap(1, 0.020, sys));
    panel.onFrame(makeSnap(2, 0.025, sys));

    const auto s = panel.summary();
    CHECK_EQ(s.samples, 2u);
    CHECK_EQ(s.firstTick, 1u);
    CHECK_EQ(s.lastTick, 2u);
    CHECK_EQ(s.systems.size(), 3u);
    CHECK(s.systems[0].name == "Slow");
    CHECK(s.systems[2].name == "Fast");

    // Render with three systems → 1 header + 3 system rows = 4 text ops.
    {
        editor::HeadlessBackend backend;
        backend.initialize();
        NullSource source;
        backend.beginFrame();
        panel.render(backend, source);
        backend.endFrame();
        CHECK_EQ(countTextOps(backend.capturedFrame()), 4u);
        backend.shutdown();
    }

    // setMaxRows clamps the system rows.
    panel.setMaxRows(2);
    {
        editor::HeadlessBackend backend;
        backend.initialize();
        NullSource source;
        backend.beginFrame();
        panel.render(backend, source);
        backend.endFrame();
        CHECK_EQ(countTextOps(backend.capturedFrame()), 3u); // header + 2
        backend.shutdown();
    }

    EXIT_WITH_RESULT();
}
