/// @file test_ui_input_capture_keyboard.cpp
/// @brief Pins keyboard capture — when the focused widget set
/// `HitTestFlags::KeyboardCapture`, the context reports `wantsKeyboard`.
/// Losing focus drops capture; focusing a widget without the flag does NOT
/// claim capture.

#include "Check.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    const WidgetID textInput{0xAA};
    const WidgetID button{0xBB};

    // Frame 1: register textInput Focusable + KeyboardCapture; press Tab so
    // textInput owns focus.
    UIInput tab;
    tab.navKeysPressed = NavKey::Tab;
    ctx.setInput(tab);
    ctx.beginFrame();
    interact(ctx, textInput, Rect{0, 0, 100, 20},
             HitTestFlags::Focusable | HitTestFlags::KeyboardCapture);
    interact(ctx, button, Rect{0, 30, 100, 20}, HitTestFlags::Focusable);
    ctx.endFrame();
    CHECK_EQ(focusedId(ctx), textInput);

    // Frame 2: re-register without Tab — focus persists. KeyboardCapture
    // is re-asserted by the textInput's interact() call.
    UIInput none;
    ctx.setInput(none);
    ctx.beginFrame();
    interact(ctx, textInput, Rect{0, 0, 100, 20},
             HitTestFlags::Focusable | HitTestFlags::KeyboardCapture);
    interact(ctx, button, Rect{0, 30, 100, 20}, HitTestFlags::Focusable);
    CHECK(wantsKeyboardCapture(ctx));
    ctx.endFrame();

    // Frame 3: Tab -> focus moves to button (no KeyboardCapture flag).
    ctx.setInput(tab);
    ctx.beginFrame();
    interact(ctx, textInput, Rect{0, 0, 100, 20},
             HitTestFlags::Focusable | HitTestFlags::KeyboardCapture);
    interact(ctx, button, Rect{0, 30, 100, 20}, HitTestFlags::Focusable);
    ctx.endFrame();
    CHECK_EQ(focusedId(ctx), button);

    // Frame 4: re-register; button has focus but no KeyboardCapture flag ->
    // wantsKeyboardCapture is false.
    ctx.setInput(none);
    ctx.beginFrame();
    interact(ctx, textInput, Rect{0, 0, 100, 20},
             HitTestFlags::Focusable | HitTestFlags::KeyboardCapture);
    interact(ctx, button, Rect{0, 30, 100, 20}, HitTestFlags::Focusable);
    CHECK(!wantsKeyboardCapture(ctx));
    ctx.endFrame();

    // Frame 5: clear focus -> wantsKeyboard is false even if textInput still
    // registered.
    clearFocus(ctx);
    ctx.setInput(none);
    ctx.beginFrame();
    interact(ctx, textInput, Rect{0, 0, 100, 20},
             HitTestFlags::Focusable | HitTestFlags::KeyboardCapture);
    CHECK(!wantsKeyboardCapture(ctx));
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
