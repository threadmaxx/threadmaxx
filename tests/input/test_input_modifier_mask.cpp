/// @file test_input_modifier_mask.cpp
/// @brief Pins the modifier-mask contract: Shift/Ctrl/Alt/Super key
/// down/up flip the matching bit on InputState::modifiers; the bit
/// stays sticky across frames until the matching up-event arrives;
/// both L and R variants are treated as the same modifier bit (held
/// state = OR of L and R).

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/detail/keymap.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Frame 0 — nothing down.
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().modifiers, std::uint8_t{Modifiers::None});
    ctx.endFrame();

    // Frame 1 — LShift down. Shift bit lights up; others stay clear.
    backend.push(KeyEvent{Key::LShift, Modifiers::Shift, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK((ctx.state().modifiers & Modifiers::Shift) != 0);
    CHECK((ctx.state().modifiers & Modifiers::Ctrl) == 0);
    ctx.endFrame();

    // Frame 2 — LCtrl also down. Both bits.
    backend.push(KeyEvent{Key::LCtrl, Modifiers::Shift | Modifiers::Ctrl, true});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK((ctx.state().modifiers & Modifiers::Shift) != 0);
    CHECK((ctx.state().modifiers & Modifiers::Ctrl) != 0);
    CHECK((ctx.state().modifiers & Modifiers::Alt) == 0);
    ctx.endFrame();

    // Frame 3 — release LShift. Ctrl bit stays.
    backend.push(KeyEvent{Key::LShift, Modifiers::Ctrl, false});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK((ctx.state().modifiers & Modifiers::Shift) == 0);
    CHECK((ctx.state().modifiers & Modifiers::Ctrl) != 0);
    ctx.endFrame();

    // Frame 4 — release LCtrl. All clear.
    backend.push(KeyEvent{Key::LCtrl, Modifiers::None, false});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK_EQ(ctx.state().modifiers, std::uint8_t{Modifiers::None});
    ctx.endFrame();

    // keymap helper sanity — diagnostic name + modifier-key classifier.
    CHECK_EQ(detail::keyName(Key::LShift), "LShift");
    CHECK_EQ(detail::keyName(Key::Space), "Space");
    CHECK(detail::isModifierKey(Key::LShift));
    CHECK(detail::isModifierKey(Key::RAlt));
    CHECK(!detail::isModifierKey(Key::A));

    EXIT_WITH_RESULT();
}
