/// @file test_ui_gizmo_drag_2d.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/gizmo.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID id{0x9201};
    const Rect h{100, 100, 16, 16};

    // Idle frame: no event.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    {
        auto ev = dragHandle2D(ctx, id, h);
        CHECK(!ev.dragging);
        CHECK(!ev.active);
    }
    ctx.endFrame();

    // Press inside: active.
    UIInput press;
    press.mousePos = Vec2i{105, 105};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    {
        auto ev = dragHandle2D(ctx, id, h);
        CHECK(ev.active);
        CHECK(!ev.dragging);  // no delta yet
    }
    ctx.endFrame();

    // Drag delta with button held: dragging + delta reported.
    UIInput drag;
    drag.mousePos = Vec2i{120, 110};
    drag.mouseDelta = Vec2i{15, 5};
    drag.mouseButtons = MouseButton::Left;
    ctx.setInput(drag);
    ctx.beginFrame();
    {
        auto ev = dragHandle2D(ctx, id, h);
        CHECK(ev.active);
        CHECK(ev.dragging);
        CHECK_EQ(ev.delta.x, 15);
        CHECK_EQ(ev.delta.y, 5);
    }
    ctx.endFrame();

    // Release: no longer active.
    UIInput rel;
    rel.mousePos = Vec2i{120, 110};
    rel.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(rel);
    ctx.beginFrame();
    {
        auto ev = dragHandle2D(ctx, id, h);
        CHECK(!ev.dragging);
    }
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
