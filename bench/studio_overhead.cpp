/// @file studio_overhead.cpp
/// @brief ST41 — Studio v1.0 perf gate.
///
/// Steady-state cost of rendering a 50-panel studio frame against the
/// headless backend. The v1.0 close-out target is **< 1 ms / frame at
/// 1080p with 50 visible widgets**.
///
/// Usage:
///   ./build/bench/studio_overhead [iterations]
///
/// Default iterations: 2000. Reports avg µs / frame for the studio
/// render phase only (excludes engine.step()); a "PASS" / "FAIL" line
/// vs. the 1 ms gate prints at the end.

#include <threadmaxx_studio/threadmaxx_studio.hpp>

#include <threadmaxx_studio/panels/console.hpp>
#include <threadmaxx_studio/panels/engine_inspector.hpp>
#include <threadmaxx_studio/panels/frame_snapshot.hpp>
#include <threadmaxx_studio/panels/gizmo.hpp>
#include <threadmaxx_studio/panels/hierarchy.hpp>
#include <threadmaxx_studio/panels/menu_bar.hpp>
#include <threadmaxx_studio/panels/profiler.hpp>
#include <threadmaxx_studio/panels/property_editor.hpp>
#include <threadmaxx_studio/panels/replay.hpp>
#include <threadmaxx_studio/panels/resources.hpp>
#include <threadmaxx_studio/panels/status_bar.hpp>
#include <threadmaxx_studio/panels/task_graph.hpp>
#include <threadmaxx_studio/panels/tuning.hpp>
#include <threadmaxx_studio/panels/world_diff.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/console.hpp>
#include <threadmaxx_editor/hierarchy.hpp>
#include <threadmaxx_editor/inspect.hpp>
#include <threadmaxx_editor/properties.hpp>
#include <threadmaxx_editor/selection.hpp>
#include <threadmaxx_editor/session.hpp>

#include <threadmaxx_reflect/registry.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct BenchGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        // Seed 64 entities so the entity inspector / hierarchy panels
        // have something to walk.
        for (int i = 0; i < 64; ++i) {
            auto e = engine.reserveEntityHandle();
            threadmaxx::Transform t{};
            t.position = {static_cast<float>(i), 0.0f, 0.0f};
            seed.spawn(e, t);
            threadmaxx::Health h{};
            h.current = 100.0f; h.max = 100.0f;
            seed.setHealth(e, h);
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    const int iters = (argc > 1) ? std::atoi(argv[1]) : 2000;

    threadmaxx::Engine engine{threadmaxx::Config{}};
    BenchGame game;
    if (!engine.initialize(game)) {
        std::fprintf(stderr, "[studio_overhead] engine.initialize FAILED\n");
        return 1;
    }

    threadmaxx::editor::HeadlessBackend backend;
    threadmaxx::editor::EditorSession   session{engine};
    session.setBackend(&backend);

    threadmaxx::editor::CommandStack    stack{engine};
    threadmaxx::editor::Inspector       inspector{engine};
    threadmaxx::editor::SelectionState  selection{engine.world()};
    threadmaxx::editor::HierarchyView   hview{engine};
    threadmaxx::editor::Console         consoleEditor{};
    threadmaxx::reflect::TypeRegistry   reflectReg;
    threadmaxx::editor::PropertyEditor  propEd{engine, reflectReg};
    propEd.addBuiltinBindings();

    threadmaxx::studio::DirectDataSource direct{engine, stack};

    // 14 engine-only panels. We resize to 50 visible widgets by
    // replicating cheap leaf panels (Console / StatusBar / MenuBar)
    // so the panel count matches the gate's "50 visible widgets"
    // target. The replication is deliberate: every additional panel
    // pays the per-render dispatch cost, so this is a worst-case
    // measurement of dispatch + draw-call funneling under the
    // headless backend.
    threadmaxx::studio::MenuBar              menuBar;
    threadmaxx::studio::StatusBar            statusBar;
    threadmaxx::studio::ConsolePanel         consolePanel{consoleEditor};
    threadmaxx::studio::EntityInspectorPanel entityPanel{inspector, selection};
    threadmaxx::studio::PropertyEditorPanel  propPanel{propEd, selection, stack};
    threadmaxx::studio::WorldDiffPanel       diffPanel;
    threadmaxx::studio::FrameSnapshotPanel   framePanel{60};
    threadmaxx::studio::TaskGraphPanel       taskPanel{engine};
    threadmaxx::studio::TuningPanel          tuningPanel{engine};
    threadmaxx::studio::ResourcesPanel       resPanel{engine, inspector};
    threadmaxx::studio::HierarchyPanel       hierPanel{hview, selection};
    threadmaxx::studio::ProfilerPanel        profPanel{256};
    threadmaxx::studio::ReplayPanel          replayPanel;

    engine.setTraceSink(&profPanel);

    constexpr int kVisiblePanels = 50;
    std::vector<threadmaxx::studio::IStudioPanel*> roster;
    roster.reserve(kVisiblePanels);
    threadmaxx::studio::IStudioPanel* base[] = {
        &menuBar, &statusBar, &consolePanel, &entityPanel, &propPanel,
        &diffPanel, &framePanel, &taskPanel, &tuningPanel, &resPanel,
        &hierPanel, &profPanel, &replayPanel,
    };
    for (int i = 0; i < kVisiblePanels; ++i) {
        roster.push_back(base[i % (sizeof(base) / sizeof(base[0]))]);
    }

    auto buildFrame = [&]() {
        backend.beginFrame();
        for (auto* p : roster) {
            p->render(backend, direct);
        }
    };

    // Warmup. Also primes the engine so engineSnapshot() returns
    // populated values throughout the measured window.
    for (int i = 0; i < 16; ++i) {
        engine.step();
        buildFrame();
    }

    using clock = std::chrono::steady_clock;
    using ns = std::chrono::nanoseconds;

    // Measure only the studio render phase. Step the engine between
    // measured frames (outside the timing window) so the data source
    // reports moving ticks.
    long long totalRenderNs = 0;
    for (int i = 0; i < iters; ++i) {
        engine.step();
        const auto t0 = clock::now();
        buildFrame();
        const auto t1 = clock::now();
        totalRenderNs +=
            std::chrono::duration_cast<ns>(t1 - t0).count();
    }

    const double avgUs =
        static_cast<double>(totalRenderNs) /
        static_cast<double>(iters) / 1000.0;
    constexpr double kGateUs = 1000.0;  // 1 ms / frame

    std::printf("[studio_overhead] iters=%d panels=%d  avg=%.2f us/frame  "
                "gate=%.0f us  %s\n",
                iters, kVisiblePanels, avgUs, kGateUs,
                avgUs < kGateUs ? "PASS" : "FAIL");

    session.setBackend(nullptr);
    engine.setTraceSink(nullptr);
    engine.shutdown();
    return avgUs < kGateUs ? 0 : 1;
}
