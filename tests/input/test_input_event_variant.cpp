/// @file test_input_event_variant.cpp
/// @brief Pins the raw InputEvent variant: every alternative round-trips,
/// the variant stays under 32 B of payload (plus a tag) so a cache line
/// holds at least one event, and emplacement preserves field values.

#include "Check.hpp"

#include <variant>

#include "threadmaxx_input/events.hpp"

int main() {
    using namespace threadmaxx::input;

    // 1) Round-trip every alternative.
    {
        InputEvent e = KeyEvent{Key::A, Modifiers::Shift, true};
        const auto* k = std::get_if<KeyEvent>(&e);
        CHECK(k != nullptr);
        CHECK_EQ(k->key, Key::A);
        CHECK_EQ(k->modifiers, std::uint8_t{Modifiers::Shift});
        CHECK(k->down);
    }
    {
        InputEvent e = CharEvent{0x2603u};
        const auto* c = std::get_if<CharEvent>(&e);
        CHECK(c != nullptr);
        CHECK_EQ(c->codepoint, std::uint32_t{0x2603u});
    }
    {
        InputEvent e = MouseMoveEvent{120.0f, 50.0f, 4.0f, -2.0f};
        const auto* m = std::get_if<MouseMoveEvent>(&e);
        CHECK(m != nullptr);
        CHECK_EQ(m->x, 120.0f);
        CHECK_EQ(m->dx, 4.0f);
    }
    {
        InputEvent e = MouseButtonEvent{MouseButton::Right, true, 1.0f, 2.0f};
        CHECK(std::holds_alternative<MouseButtonEvent>(e));
    }
    {
        InputEvent e = MouseWheelEvent{0.0f, 1.5f};
        CHECK(std::holds_alternative<MouseWheelEvent>(e));
    }
    {
        InputEvent e = GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true};
        CHECK(std::holds_alternative<GamepadButtonEvent>(e));
    }
    {
        InputEvent e = GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.75f};
        const auto* a = std::get_if<GamepadAxisEvent>(&e);
        CHECK(a != nullptr);
        CHECK_EQ(a->value, 0.75f);
    }
    {
        InputEvent e = DeviceConnectEvent{kGamepad0DeviceId, true};
        CHECK(std::holds_alternative<DeviceConnectEvent>(e));
    }
    {
        InputEvent e = DeviceDisconnectEvent{kGamepad0DeviceId};
        CHECK(std::holds_alternative<DeviceDisconnectEvent>(e));
    }

    // 2) Variant size budget. MouseMoveEvent is the largest payload (16 B).
    //    sizeof(std::variant) is payload + tag; allow up to 32 B total so
    //    at least one event fits a half cache line.
    CHECK(sizeof(InputEvent) <= 32);

    EXIT_WITH_RESULT();
}
