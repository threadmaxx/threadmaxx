/// @file test_ui_input_capture_mouse.cpp
/// @brief Pins mouse capture — while a widget is "active" (left button
/// pressed and held inside it), `wantsMouseCapture()` is true. Click outside
/// the active widget does not steal it; release inside fires `.clicked`.

#include "Check.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID idA{0xA};
    const WidgetID idB{0xB};

    // Frame 1: no input -> neither hovered nor active.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    interact(ctx, idA, Rect{0, 0, 100, 100});
    interact(ctx, idB, Rect{200, 0, 100, 100});
    CHECK(!wantsMouseCapture(ctx));
    ctx.endFrame();

    // Frame 2: mouse over A + left pressed -> A becomes active.
    UIInput press;
    press.mousePos = Vec2i{50, 50};
    press.mouseButtons = MouseButton::Left;
    press.mouseButtonsPressed = MouseButton::Left;
    ctx.setInput(press);
    ctx.beginFrame();
    {
        const auto r = interact(ctx, idA, Rect{0, 0, 100, 100});
        CHECK(r.hovered);
        CHECK(r.active);
        CHECK(!r.clicked);
    }
    interact(ctx, idB, Rect{200, 0, 100, 100});
    CHECK(isActive(ctx, idA));
    CHECK(wantsMouseCapture(ctx));
    ctx.endFrame();

    // Frame 3: mouse dragged onto B but still held — A stays active.
    UIInput hold;
    hold.mousePos = Vec2i{250, 50};
    hold.mouseButtons = MouseButton::Left;
    ctx.setInput(hold);
    ctx.beginFrame();
    interact(ctx, idA, Rect{0, 0, 100, 100});
    interact(ctx, idB, Rect{200, 0, 100, 100});
    CHECK(isActive(ctx, idA));
    CHECK(!isActive(ctx, idB));
    CHECK(wantsMouseCapture(ctx));
    ctx.endFrame();

    // Frame 4: release outside A — active clears, no click event for A.
    UIInput releaseOutside;
    releaseOutside.mousePos = Vec2i{250, 50};
    releaseOutside.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(releaseOutside);
    ctx.beginFrame();
    {
        const auto r = interact(ctx, idA, Rect{0, 0, 100, 100});
        CHECK(!r.clicked);
    }
    interact(ctx, idB, Rect{200, 0, 100, 100});
    CHECK(!wantsMouseCapture(ctx));
    ctx.endFrame();

    // Frame 5: press inside A, frame 6: release inside A -> .clicked fires.
    ctx.setInput(press);
    ctx.beginFrame();
    interact(ctx, idA, Rect{0, 0, 100, 100});
    ctx.endFrame();
    CHECK(isActive(ctx, idA));

    UIInput releaseInside;
    releaseInside.mousePos = Vec2i{50, 50};
    releaseInside.mouseButtonsReleased = MouseButton::Left;
    ctx.setInput(releaseInside);
    ctx.beginFrame();
    {
        const auto r = interact(ctx, idA, Rect{0, 0, 100, 100});
        CHECK(r.clicked);
    }
    ctx.endFrame();
    CHECK(!wantsMouseCapture(ctx));

    EXIT_WITH_RESULT();
}
