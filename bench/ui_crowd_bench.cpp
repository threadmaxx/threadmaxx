/// @file ui_crowd_bench.cpp
/// @brief Steady-state cost of building a 500-widget UI frame across 8
/// panels. Threadmaxx_ui's v1.0 perf gate.
///
/// Usage:
///   ./build/bench/ui_crowd_bench [iterations]
///
/// Default iterations: 2000. Reports avg µs / frame for the UI build phase
/// + the vertex backend's tessellation cost separately. The v1.0 gate is
/// **< 1 ms / frame for the build phase** (excludes renderer upload).

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "threadmaxx_ui/backends/VertexBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"
#include "threadmaxx_ui/panel.hpp"
#include "threadmaxx_ui/widget.hpp"

int main(int argc, char** argv) {
    using namespace threadmaxx::ui;

    const int iters = (argc > 1) ? std::atoi(argv[1]) : 2000;

    UIContext ctx;
    VertexBackend backend;
    backend.reserve(16384, 4096);
    ctx.setBackend(&backend);
    ctx.reserveHitTests(512);
    ctx.reserveWidgetStates(512);
    setHostRect(ctx, Rect{0, 0, 1920, 1080});

    constexpr int kPanels = 8;
    constexpr int kWidgetsPerPanel = 64;  // -> 512 widgets total
    PanelState panels[kPanels];
    for (int p = 0; p < kPanels; ++p) {
        panels[p].bounds = Rect{
            (p % 4) * 480, (p / 4) * 540, 470, 530};
    }
    bool boolVals[kPanels * kWidgetsPerPanel] = {};
    float floatVals[kPanels * kWidgetsPerPanel] = {};

    auto buildFrame = [&]() {
        UIInput none;
        ctx.setInput(none);
        ctx.beginFrame();
        for (int p = 0; p < kPanels; ++p) {
            const WidgetID panelId{0x9000 + static_cast<std::uint64_t>(p)};
            if (beginPanel(ctx, panelId, "Inspector", panels[p])) {
                for (int w = 0; w < kWidgetsPerPanel; ++w) {
                    const int idx = p * kWidgetsPerPanel + w;
                    const Rect row{
                        panels[p].bounds.x + 8,
                        panels[p].bounds.y + 24 + w * 8,
                        panels[p].bounds.w - 16, 8};
                    if ((w & 1) == 0) {
                        inspect(ctx, WidgetID{static_cast<std::uint64_t>(0x10000 + idx)},
                                row, "lbl", &boolVals[idx]);
                    } else {
                        inspect(ctx, WidgetID{static_cast<std::uint64_t>(0x20000 + idx)},
                                row, "lbl", &floatVals[idx]);
                    }
                }
                endPanel(ctx);
            }
        }
        ctx.endFrame();
    };

    // Warmup.
    for (int i = 0; i < 8; ++i) buildFrame();

    using clock = std::chrono::steady_clock;
    using ns = std::chrono::nanoseconds;
    const auto t0 = clock::now();
    for (int i = 0; i < iters; ++i) {
        buildFrame();
    }
    const auto t1 = clock::now();
    const double totalUs = std::chrono::duration_cast<ns>(t1 - t0).count() / 1000.0;
    const double avgUs = totalUs / static_cast<double>(iters);

    const std::size_t cmdCount = ctx.drawList().commands().size();
    const std::size_t vtxCount = backend.vertices().size();
    const std::size_t drawCount = backend.draws().size();

    std::printf("ui_crowd_bench results:\n");
    std::printf("  iterations : %d\n", iters);
    std::printf("  per-frame  : %.3f us (%.3f ms)\n", avgUs, avgUs / 1000.0);
    std::printf("  last frame : %zu draw cmds, %zu vertices, %zu draws\n",
                cmdCount, vtxCount, drawCount);
    std::printf("  panels     : %d, widgets/panel : %d, total widgets : %d\n",
                kPanels, kWidgetsPerPanel, kPanels * kWidgetsPerPanel);
    std::printf("  gate       : %s 1 ms/frame\n",
                avgUs < 1000.0 ? "PASS <" : "FAIL >=");

    return avgUs < 1000.0 ? 0 : 1;
}
