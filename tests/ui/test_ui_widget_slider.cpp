/// @file test_ui_widget_slider.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID id{0x51};
    const Rect r{0, 0, 200, 20};
    float v = 0.0f;

    // Press at midpoint -> value snaps to 0.5*(max-min).
    UIInput press;
    press.mousePos = Vec2i{100, 10};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    CHECK(slider(ctx, id, r, &v, 0.0f, 1.0f));
    ctx.endFrame();
    CHECK(v > 0.49f && v < 0.51f);

    // Drag to x=200 -> value clamps to max.
    UIInput drag;
    drag.mousePos = Vec2i{300, 10};  // past the right edge
    drag.mouseButtons = MouseButton::Left;
    ctx.setInput(drag);
    ctx.beginFrame();
    slider(ctx, id, r, &v, 0.0f, 1.0f);
    ctx.endFrame();
    CHECK(v >= 0.999f);

    // Drag past left edge -> clamp to min.
    UIInput dragLeft;
    dragLeft.mousePos = Vec2i{-50, 10};
    dragLeft.mouseButtons = MouseButton::Left;
    ctx.setInput(dragLeft);
    ctx.beginFrame();
    slider(ctx, id, r, &v, 0.0f, 1.0f);
    ctx.endFrame();
    CHECK(v <= 0.001f);

    // No-press frame leaves v alone.
    const float before = v;
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    CHECK(!slider(ctx, id, r, &v, 0.0f, 1.0f));
    ctx.endFrame();
    CHECK(v == before);

    EXIT_WITH_RESULT();
}
