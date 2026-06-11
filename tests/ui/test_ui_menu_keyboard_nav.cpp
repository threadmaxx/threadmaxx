/// @file test_ui_menu_keyboard_nav.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/menu.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID popup{0xF301};
    const WidgetID i1{0xF311};
    const WidgetID i2{0xF312};
    const WidgetID i3{0xF313};

    openPopup(ctx, popup);

    bool firedI2 = false;

    auto runFrame = [&](const UIInput& in) {
        ctx.setInput(in);
        ctx.beginFrame();
        if (beginPopup(ctx, popup, Rect{0, 0, 200, 100})) {
            menuItem(ctx, i1, Rect{0,  0, 200, 20}, "One");
            if (menuItem(ctx, i2, Rect{0, 20, 200, 20}, "Two")) firedI2 = true;
            menuItem(ctx, i3, Rect{0, 40, 200, 20}, "Three");
            endPopup(ctx);
        }
        ctx.endFrame();
    };

    // Down arrows walk through items 1 -> 2 -> 3.
    UIInput down;
    down.navKeysPressed = NavKey::Down;
    runFrame(down);
    CHECK_EQ(focusedId(ctx), i1);
    runFrame(down);
    CHECK_EQ(focusedId(ctx), i2);
    runFrame(down);
    CHECK_EQ(focusedId(ctx), i3);

    // Up walks backwards.
    UIInput up;
    up.navKeysPressed = NavKey::Up;
    runFrame(up);
    CHECK_EQ(focusedId(ctx), i2);

    // Enter on the focused item -> menuItem returns true; popup closes.
    UIInput enter;
    enter.navKeysPressed = NavKey::Enter;
    runFrame(enter);
    CHECK(firedI2);
    CHECK(!isPopupOpen(ctx, popup));

    // Re-open and try Escape — popup closes; focus state untouched.
    openPopup(ctx, popup);
    runFrame(down);                 // focus -> i1
    UIInput esc;
    esc.navKeysPressed = NavKey::Escape;
    runFrame(esc);
    CHECK(!isPopupOpen(ctx, popup));

    EXIT_WITH_RESULT();
}
