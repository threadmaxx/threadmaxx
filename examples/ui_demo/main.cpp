/// @file examples/ui_demo/main.cpp
/// @brief Headless walkthrough of every threadmaxx_ui pillar. Runs 600
/// frames of mixed widgets through the VertexBackend, prints summary
/// stats, and exits cleanly.
///
/// This is the integration smoke for v1.0 — the bench measures cost,
/// this demo proves the surface fits together.

#include <cstdio>
#include <cstdlib>

#include "threadmaxx_ui/backends/VertexBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/debug.hpp"
#include "threadmaxx_ui/dragdrop.hpp"
#include "threadmaxx_ui/gizmo.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"
#include "threadmaxx_ui/menu.hpp"
#include "threadmaxx_ui/panel.hpp"
#include "threadmaxx_ui/tree.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace {

using namespace threadmaxx::ui;

struct DemoState {
    PanelState propsPanel;
    PanelState treePanel;
    bool checked = false;
    std::int32_t intVal = 42;
    float floatVal = 1.5f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    char nameBuf[32] = "demo";
    enum class Mode { Off, Hold, Loop };
    Mode mode = Mode::Off;
};

constexpr std::pair<DemoState::Mode, std::string_view> kModeOpts[3] = {
    {DemoState::Mode::Off,  "Off"},
    {DemoState::Mode::Hold, "Hold"},
    {DemoState::Mode::Loop, "Loop"}};

void buildFrame(UIContext& ctx, DemoState& state, std::uint64_t frame) {
    UIInput in;
    in.deltaTimeSeconds = 1.0f / 60.0f;
    in.mousePos = Vec2i{static_cast<std::int32_t>(frame % 1920),
                        static_cast<std::int32_t>(frame % 1080)};
    ctx.setInput(in);

    ctx.beginFrame();

    // -- Top bar with menu + HUD overlay
    beginMenuBar(ctx, Rect{0, 0, 1920, 22});
    if (beginMenu(ctx, WidgetID{0x1}, Rect{4, 2, 60, 20}, "File")) {
        menuItem(ctx, WidgetID{0x11}, Rect{4, 24, 120, 20}, "New");
        menuItem(ctx, WidgetID{0x12}, Rect{4, 46, 120, 20}, "Open");
        menuItem(ctx, WidgetID{0x13}, Rect{4, 68, 120, 20}, "Save");
        endMenu(ctx);
    }
    if (beginMenu(ctx, WidgetID{0x2}, Rect{68, 2, 60, 20}, "Edit")) {
        menuItem(ctx, WidgetID{0x21}, Rect{68, 24, 120, 20}, "Undo");
        menuItem(ctx, WidgetID{0x22}, Rect{68, 46, 120, 20}, "Redo");
        endMenu(ctx);
    }
    endMenuBar(ctx);

    // -- Properties panel: inspector rows
    if (beginPanel(ctx, WidgetID{0x100}, "Properties", state.propsPanel)) {
        std::int32_t y = state.propsPanel.bounds.y + 24;
        const std::int32_t w = state.propsPanel.bounds.w - 12;
        inspect(ctx, WidgetID{0x101}, Rect{state.propsPanel.bounds.x + 6, y, w, 20},
                "Enabled", &state.checked); y += 22;
        inspect(ctx, WidgetID{0x102}, Rect{state.propsPanel.bounds.x + 6, y, w, 20},
                "Count", &state.intVal); y += 22;
        inspect(ctx, WidgetID{0x103}, Rect{state.propsPanel.bounds.x + 6, y, w, 20},
                "Speed", &state.floatVal); y += 22;
        inspect(ctx, WidgetID{0x104}, Rect{state.propsPanel.bounds.x + 6, y, w, 20},
                "Pos", &state.vx, &state.vy, &state.vz); y += 22;
        inspect(ctx, WidgetID{0x105}, Rect{state.propsPanel.bounds.x + 6, y, w, 20},
                "Name", state.nameBuf, sizeof(state.nameBuf)); y += 22;
        inspectEnum(ctx, WidgetID{0x106}, Rect{state.propsPanel.bounds.x + 6, y, w, 20},
                    "Mode", &state.mode,
                    std::span<const std::pair<DemoState::Mode, std::string_view>>{kModeOpts, 3});
        endPanel(ctx);
    }

    // -- Tree panel
    if (beginPanel(ctx, WidgetID{0x200}, "Hierarchy", state.treePanel)) {
        const std::int32_t bx = state.treePanel.bounds.x + 4;
        std::int32_t by = state.treePanel.bounds.y + 24;
        if (treeNodeBegin(ctx, WidgetID{0x201}, Rect{bx, by, state.treePanel.bounds.w - 8, 20}, "World")) {
            by += 22;
            treeNodeBegin(ctx, WidgetID{0x202}, Rect{bx + 20, by, state.treePanel.bounds.w - 28, 20}, "Player");
            treeNodeEnd(ctx);
            by += 22;
            treeNodeBegin(ctx, WidgetID{0x203}, Rect{bx + 20, by, state.treePanel.bounds.w - 28, 20}, "Enemy A");
            treeNodeEnd(ctx);
            treeNodeEnd(ctx);
        }
        endPanel(ctx);
    }

    // -- Debug HUD
    debug::beginHud(ctx, 4, 24);
    debug::kvInt(ctx, "frame", static_cast<std::int64_t>(frame));
    debug::kvInt(ctx, "draw cmds", static_cast<std::int64_t>(ctx.drawList().commands().size()));

    ctx.endFrame();
}

} // namespace

int main(int argc, char** argv) {
    using namespace threadmaxx::ui;

    int frames = 600;
    if (argc > 1) frames = std::atoi(argv[1]);

    UIContext ctx;
    VertexBackend backend;
    backend.reserve(8192, 1024);
    ctx.setBackend(&backend);
    setHostRect(ctx, Rect{0, 0, 1920, 1080});

    DemoState state;
    state.propsPanel.bounds = Rect{20, 40, 360, 360};
    state.treePanel.bounds = Rect{400, 40, 280, 360};
    setTreeOpen(ctx, WidgetID{0x201}, true);

    for (int i = 0; i < frames; ++i) {
        buildFrame(ctx, state, static_cast<std::uint64_t>(i));
    }

    std::printf("ui_demo: ran %d frames\n", frames);
    std::printf("  last frame: %zu draw cmds, %zu vertices, %zu draws\n",
                ctx.drawList().commands().size(),
                backend.vertices().size(),
                backend.draws().size());
    std::printf("  final state: enabled=%d count=%d speed=%.3f mode=%d\n",
                state.checked ? 1 : 0, state.intVal,
                static_cast<double>(state.floatVal),
                static_cast<int>(state.mode));
    return 0;
}
