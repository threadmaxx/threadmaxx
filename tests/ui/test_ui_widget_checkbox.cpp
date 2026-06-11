/// @file test_ui_widget_checkbox.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID id{0xC1};
    const Rect r{0, 0, 120, 20};
    bool value = false;

    // Press + release inside toggles to true.
    UIInput press;
    press.mousePos = Vec2i{5, 5};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    CHECK(!checkbox(ctx, id, r, "Enable", &value));  // press only
    ctx.endFrame();
    UIInput rel;
    rel.mousePos = Vec2i{5, 5};
    rel.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(rel);
    ctx.beginFrame();
    CHECK(checkbox(ctx, id, r, "Enable", &value));
    ctx.endFrame();
    CHECK(value == true);

    // Second click toggles back to false.
    ctx.setInput(press);
    ctx.beginFrame();
    checkbox(ctx, id, r, "Enable", &value);
    ctx.endFrame();
    ctx.setInput(rel);
    ctx.beginFrame();
    CHECK(checkbox(ctx, id, r, "Enable", &value));
    ctx.endFrame();
    CHECK(value == false);

    // No-input frames don't toggle.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    CHECK(!checkbox(ctx, id, r, "Enable", &value));
    ctx.endFrame();
    CHECK(value == false);

    EXIT_WITH_RESULT();
}
