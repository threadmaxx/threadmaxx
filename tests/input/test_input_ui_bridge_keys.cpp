/// @file test_input_ui_bridge_keys.cpp
/// @brief Modifiers map 1:1; nav keys surface in navKeysPressed; char
/// queue copies into UIInput::chars.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/ui_bridge.hpp"
#include "threadmaxx_ui/input.hpp"

int main() {
    using namespace threadmaxx::input;
    namespace UM = threadmaxx::ui::Modifiers;
    namespace UK = threadmaxx::ui::NavKey;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 1 — Shift held, Tab pressed → ShiftTab nav bit.
    backend.push(KeyEvent{Key::LShift, Modifiers::Shift, true});
    backend.push(KeyEvent{Key::Tab, Modifiers::Shift, true});
    backend.push(CharEvent{'h'});
    backend.push(CharEvent{'i'});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto ui = toUIInput(ctx);
        CHECK((ui.modifiers & UM::Shift) != 0);
        CHECK((ui.modifiers & UM::Ctrl) == 0);
        CHECK((ui.navKeysPressed & UK::ShiftTab) != 0);
        CHECK((ui.navKeysPressed & UK::Tab) == 0);
        CHECK_EQ(ui.charsCount, std::uint8_t{2});
        CHECK_EQ(ui.chars[0], 'h');
        CHECK_EQ(ui.chars[1], 'i');
    }
    ctx.endFrame();

    // Frame 2 — release Tab so a fresh edge can fire in frame 3.
    backend.push(KeyEvent{Key::Tab, Modifiers::Shift, false});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto ui = toUIInput(ctx);
        CHECK_EQ(ui.navKeysPressed, std::uint16_t{0});
        CHECK((ui.modifiers & UM::Shift) != 0);
        CHECK_EQ(ui.charsCount, std::uint8_t{0});
    }
    ctx.endFrame();

    // Frame 3 — release Shift, press Tab plain. Tab nav bit (not ShiftTab).
    backend.push(KeyEvent{Key::LShift, Modifiers::None, false});
    backend.push(KeyEvent{Key::Tab, Modifiers::None, true});
    backend.push(KeyEvent{Key::Escape, Modifiers::None, true});
    backend.push(KeyEvent{Key::Enter, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto ui = toUIInput(ctx);
        CHECK((ui.modifiers & UM::Shift) == 0);
        CHECK((ui.navKeysPressed & UK::Tab) != 0);
        CHECK((ui.navKeysPressed & UK::ShiftTab) == 0);
        CHECK((ui.navKeysPressed & UK::Escape) != 0);
        CHECK((ui.navKeysPressed & UK::Enter) != 0);
    }
    ctx.endFrame();

    // Frame 4 — arrow keys mapped to their nav bits.
    backend.push(KeyEvent{Key::Left, Modifiers::None, true});
    backend.push(KeyEvent{Key::Right, Modifiers::None, true});
    backend.push(KeyEvent{Key::Up, Modifiers::None, true});
    backend.push(KeyEvent{Key::Down, Modifiers::None, true});
    backend.push(KeyEvent{Key::Backspace, Modifiers::None, true});
    backend.push(KeyEvent{Key::Delete, Modifiers::None, true});
    backend.push(KeyEvent{Key::Home, Modifiers::None, true});
    backend.push(KeyEvent{Key::End, Modifiers::None, true});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto ui = toUIInput(ctx);
        CHECK((ui.navKeysPressed & UK::Left) != 0);
        CHECK((ui.navKeysPressed & UK::Right) != 0);
        CHECK((ui.navKeysPressed & UK::Up) != 0);
        CHECK((ui.navKeysPressed & UK::Down) != 0);
        CHECK((ui.navKeysPressed & UK::Backspace) != 0);
        CHECK((ui.navKeysPressed & UK::Delete) != 0);
        CHECK((ui.navKeysPressed & UK::Home) != 0);
        CHECK((ui.navKeysPressed & UK::End) != 0);
    }
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
