#pragma once

#include <cstdint>

namespace threadmaxx::input {

using DeviceId = std::uint16_t;

inline constexpr DeviceId kKeyboardDeviceId = 0;
inline constexpr DeviceId kMouseDeviceId = 1;
inline constexpr DeviceId kGamepad0DeviceId = 2;
inline constexpr DeviceId kGamepadDeviceIdBase = kGamepad0DeviceId;

inline constexpr DeviceId gamepadDeviceId(std::uint16_t slot) noexcept {
    return static_cast<DeviceId>(kGamepadDeviceIdBase + slot);
}

// 32-bit hash of the action name (FNV-1a-64 truncated). Stable across builds.
using ActionId = std::uint32_t;

// ~120 keys, fits in a 256-bit bitset. Underlying values are stable but not
// guaranteed wire-stable across releases; serialize as scancode strings if
// you need cross-version persistence (out of scope for v1.0).
enum class Key : std::uint16_t {
    Unknown = 0,

    // Letters
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Digits (top row).
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Function keys.
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

    // Navigation + editing.
    Space, Enter, Tab, Escape, Backspace, Delete, Insert,
    Left, Right, Up, Down,
    Home, End, PageUp, PageDown,

    // Punctuation.
    Minus, Equal, LeftBracket, RightBracket, Backslash, Semicolon, Quote,
    Comma, Period, Slash, Grave,

    // Modifiers.
    LShift, RShift, LCtrl, RCtrl, LAlt, RAlt, LSuper, RSuper,

    // Numpad.
    Numpad0, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadAdd, NumpadSubtract, NumpadMultiply, NumpadDivide,
    NumpadEnter, NumpadDecimal,

    // Sentinel — keep last. Bitset cap derives from this.
    Count
};

inline constexpr std::uint16_t kKeyBitsetWords = (static_cast<std::uint16_t>(Key::Count) + 63u) / 64u;

enum class MouseButton : std::uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    X1 = 3,
    X2 = 4,
    Count
};

enum class GamepadButton : std::uint8_t {
    A = 0,
    B,
    X,
    Y,
    LBumper,
    RBumper,
    Back,
    Start,
    Guide,
    LStick,
    RStick,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count
};

enum class GamepadAxis : std::uint8_t {
    LStickX = 0,
    LStickY,
    RStickX,
    RStickY,
    LTrigger,
    RTrigger,
    Count
};

namespace Modifiers {
inline constexpr std::uint8_t None  = 0;
inline constexpr std::uint8_t Shift = 1u << 0;
inline constexpr std::uint8_t Ctrl  = 1u << 1;
inline constexpr std::uint8_t Alt   = 1u << 2;
inline constexpr std::uint8_t Super = 1u << 3;
}  // namespace Modifiers

}  // namespace threadmaxx::input
