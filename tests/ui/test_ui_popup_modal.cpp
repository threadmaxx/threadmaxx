/// @file test_ui_popup_modal.cpp

#include "Check.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/menu.hpp"
#include "threadmaxx_ui/widget.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID popup{0xF201};
    const WidgetID popupItem{0xF202};
    const WidgetID bgButton{0xF203};

    openPopup(ctx, popup);
    CHECK(isPopupOpen(ctx, popup));

    auto runFrame = [&](const UIInput& in) {
        ctx.setInput(in);
        ctx.beginFrame();
        // Background button — must be shadowed while popup is open.
        button(ctx, bgButton, Rect{500, 0, 100, 30}, "BG");
        if (beginPopup(ctx, popup, Rect{0, 0, 200, 100})) {
            menuItem(ctx, popupItem, Rect{0, 0, 200, 20}, "Item");
            endPopup(ctx);
        }
        ctx.endFrame();
    };

    // Click on the background button while the popup is open — bg button
    // should NOT receive the click; click-outside closes the popup.
    UIInput pressBg;
    pressBg.mousePos = Vec2i{520, 5};
    pressBg.mouseButtons = MouseButton::Left;
    pressBg.mouseButtonsPressed = MouseButton::Left;
    runFrame(pressBg);
    CHECK(!isActive(ctx, bgButton));
    CHECK(!isPopupOpen(ctx, popup));

    // Re-open and try Escape.
    openPopup(ctx, popup);
    CHECK(isPopupOpen(ctx, popup));
    UIInput escape;
    escape.navKeysPressed = NavKey::Escape;
    runFrame(escape);
    CHECK(!isPopupOpen(ctx, popup));

    // Re-open and click an item inside the popup — popup closes after the
    // item fires.
    openPopup(ctx, popup);
    UIInput pressItem;
    pressItem.mousePos = Vec2i{10, 5};
    pressItem.mouseButtons = MouseButton::Left;
    pressItem.mouseButtonsPressed = MouseButton::Left;
    runFrame(pressItem);
    CHECK(isPopupOpen(ctx, popup));
    UIInput relItem;
    relItem.mousePos = Vec2i{10, 5};
    relItem.mouseButtonsReleased = MouseButton::Left;
    runFrame(relItem);
    CHECK(!isPopupOpen(ctx, popup));

    EXIT_WITH_RESULT();
}
