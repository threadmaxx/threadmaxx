/// @file test_ui_panel_move.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/panel.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    setHostRect(ctx, Rect{0, 0, 800, 600});
    PanelState ps;
    ps.bounds = Rect{100, 100, 200, 150};
    const WidgetID id{0x9001};

    auto runFrame = [&](const UIInput& in) {
        ctx.setInput(in);
        ctx.beginFrame();
        if (beginPanel(ctx, id, "Panel", ps)) endPanel(ctx);
        ctx.endFrame();
    };

    // Frame 1: press inside title bar.
    UIInput press;
    press.mousePos = Vec2i{120, 105};  // inside title bar (top 22 px)
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    runFrame(press);

    // Frame 2: drag +30, +20 with button held.
    UIInput drag;
    drag.mousePos = Vec2i{150, 125};
    drag.mouseDelta = Vec2i{30, 20};
    drag.mouseButtons = MouseButton::Left;
    runFrame(drag);
    CHECK_EQ(ps.bounds.x, 130);
    CHECK_EQ(ps.bounds.y, 120);

    // Frame 3: release.
    UIInput rel;
    rel.mousePos = Vec2i{150, 125};
    rel.mouseButtonsReleased = MouseButton::Left;
    runFrame(rel);

    // Bounds clamp to host rect. Drag panel far right/down, should clamp.
    UIInput pressBR;
    pressBR.mousePos = Vec2i{135, 125};  // re-press inside (now moved)
    pressBR.mouseButtons = MouseButton::Left;
    pressBR.mouseButtonsPressed = MouseButton::Left;
    runFrame(pressBR);
    UIInput hugeDrag;
    hugeDrag.mousePos = Vec2i{2000, 2000};
    hugeDrag.mouseDelta = Vec2i{2000, 2000};
    hugeDrag.mouseButtons = MouseButton::Left;
    runFrame(hugeDrag);
    CHECK(ps.bounds.x + ps.bounds.w <= 800);
    CHECK(ps.bounds.y + ps.bounds.h <= 600);
    runFrame(rel);

    // Insert filler frames so any previous click drops out of the
    // double-click window before we start the dedicated sequence.
    UIInput idle;
    for (int i = 0; i < 30; ++i) runFrame(idle);

    // Double-click title bar collapses.
    auto clickTitle = [&]() {
        UIInput p;
        p.mousePos = Vec2i{ps.bounds.x + 10, ps.bounds.y + 5};
        p.mouseButtons = MouseButton::Left;
        p.mouseButtonsPressed = MouseButton::Left;
        runFrame(p);
        UIInput r;
        r.mousePos = Vec2i{ps.bounds.x + 10, ps.bounds.y + 5};
        r.mouseButtonsReleased = MouseButton::Left;
        runFrame(r);
    };
    clickTitle();
    CHECK(!ps.collapsed);
    clickTitle();        // within double-click window
    CHECK(ps.collapsed);
    // Collapsed panel: beginPanel returns false.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    CHECK(!beginPanel(ctx, id, "Panel", ps));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
