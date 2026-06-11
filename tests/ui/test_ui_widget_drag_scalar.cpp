/// @file test_ui_widget_drag_scalar.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID id{0xD01};
    const Rect r{0, 0, 60, 20};
    float v = 0.0f;

    // Press inside without delta -> no change.
    UIInput press;
    press.mousePos = Vec2i{10, 10};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    CHECK(!dragScalar(ctx, id, r, &v, 0.1f));
    ctx.endFrame();

    // Drag right 10 px -> v += 10 * 0.1 = 1.0.
    UIInput drag;
    drag.mousePos = Vec2i{20, 10};
    drag.mouseDelta = Vec2i{10, 0};
    drag.mouseButtons = MouseButton::Left;
    ctx.setInput(drag);
    ctx.beginFrame();
    CHECK(dragScalar(ctx, id, r, &v, 0.1f));
    ctx.endFrame();
    CHECK(v > 0.99f && v < 1.01f);

    // Ctrl halves the speed.
    UIInput slow;
    slow.mousePos = Vec2i{30, 10};
    slow.mouseDelta = Vec2i{10, 0};
    slow.mouseButtons = MouseButton::Left;
    slow.modifiers = Modifiers::Ctrl;
    ctx.setInput(slow);
    const float before = v;
    ctx.beginFrame();
    dragScalar(ctx, id, r, &v, 0.1f);
    ctx.endFrame();
    CHECK(v - before > 0.49f && v - before < 0.51f);

    // Shift doubles it.
    UIInput fast;
    fast.mousePos = Vec2i{40, 10};
    fast.mouseDelta = Vec2i{10, 0};
    fast.mouseButtons = MouseButton::Left;
    fast.modifiers = Modifiers::Shift;
    ctx.setInput(fast);
    const float before2 = v;
    ctx.beginFrame();
    dragScalar(ctx, id, r, &v, 0.1f);
    ctx.endFrame();
    CHECK(v - before2 > 1.99f && v - before2 < 2.01f);

    EXIT_WITH_RESULT();
}
