/// @file test_ui_widget_button.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID id{0xB1};
    const Rect r{10, 10, 100, 30};

    // Frame 1: no input — no click.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    CHECK(!button(ctx, id, r, "OK"));
    ctx.endFrame();

    // Frame 2: press inside.
    UIInput press;
    press.mousePos = Vec2i{20, 20};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    CHECK(!button(ctx, id, r, "OK"));  // press only — no click yet
    ctx.endFrame();
    CHECK(isActive(ctx, id));

    // Frame 3: release inside -> click.
    UIInput rel;
    rel.mousePos = Vec2i{20, 20};
    rel.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(rel);
    ctx.beginFrame();
    CHECK(button(ctx, id, r, "OK"));
    ctx.endFrame();

    // Frame 4: press inside, frame 5: release OUTSIDE -> no click.
    ctx.setInput(press);
    ctx.beginFrame();
    button(ctx, id, r, "OK");
    ctx.endFrame();
    UIInput relOutside;
    relOutside.mousePos = Vec2i{500, 500};
    relOutside.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(relOutside);
    ctx.beginFrame();
    CHECK(!button(ctx, id, r, "OK"));
    ctx.endFrame();

    // Disabled button absorbs hover but never clicks.
    ctx.setInput(press);
    ctx.beginFrame();
    ButtonStyle dis;
    dis.disabled = true;
    CHECK(!button(ctx, id, r, "OK", dis));
    CHECK(!isActive(ctx, id));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
