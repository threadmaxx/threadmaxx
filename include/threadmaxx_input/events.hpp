#pragma once

#include <cstdint>
#include <variant>

#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input {

struct KeyEvent {
    Key key{Key::Unknown};
    std::uint8_t modifiers{};
    bool down{};
};

struct CharEvent {
    std::uint32_t codepoint{};
};

struct MouseMoveEvent {
    float x{};
    float y{};
    float dx{};
    float dy{};
};

struct MouseButtonEvent {
    MouseButton button{MouseButton::Left};
    bool down{};
    float x{};
    float y{};
};

struct MouseWheelEvent {
    float dx{};
    float dy{};
};

struct GamepadButtonEvent {
    DeviceId device{kGamepad0DeviceId};
    GamepadButton button{GamepadButton::A};
    bool down{};
};

struct GamepadAxisEvent {
    DeviceId device{kGamepad0DeviceId};
    GamepadAxis axis{GamepadAxis::LStickX};
    float value{};
};

struct DeviceConnectEvent {
    DeviceId device{};
    bool gamepad{};
};

struct DeviceDisconnectEvent {
    DeviceId device{};
};

using InputEvent = std::variant<
    KeyEvent,
    CharEvent,
    MouseMoveEvent,
    MouseButtonEvent,
    MouseWheelEvent,
    GamepadButtonEvent,
    GamepadAxisEvent,
    DeviceConnectEvent,
    DeviceDisconnectEvent>;

}  // namespace threadmaxx::input
