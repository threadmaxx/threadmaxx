#pragma once

#include <string_view>

#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input::detail {

// Stable diagnostic name for a Key. The string mirrors the enum name so
// it round-trips with `keyFromName`. Used by binding (de)serialization
// (I3) and by logging; never display-formatted.
constexpr std::string_view keyName(Key k) noexcept {
    switch (k) {
        case Key::Unknown: return "Unknown";
        case Key::A: return "A"; case Key::B: return "B"; case Key::C: return "C";
        case Key::D: return "D"; case Key::E: return "E"; case Key::F: return "F";
        case Key::G: return "G"; case Key::H: return "H"; case Key::I: return "I";
        case Key::J: return "J"; case Key::K: return "K"; case Key::L: return "L";
        case Key::M: return "M"; case Key::N: return "N"; case Key::O: return "O";
        case Key::P: return "P"; case Key::Q: return "Q"; case Key::R: return "R";
        case Key::S: return "S"; case Key::T: return "T"; case Key::U: return "U";
        case Key::V: return "V"; case Key::W: return "W"; case Key::X: return "X";
        case Key::Y: return "Y"; case Key::Z: return "Z";

        case Key::Num0: return "Num0"; case Key::Num1: return "Num1";
        case Key::Num2: return "Num2"; case Key::Num3: return "Num3";
        case Key::Num4: return "Num4"; case Key::Num5: return "Num5";
        case Key::Num6: return "Num6"; case Key::Num7: return "Num7";
        case Key::Num8: return "Num8"; case Key::Num9: return "Num9";

        case Key::F1: return "F1"; case Key::F2: return "F2";
        case Key::F3: return "F3"; case Key::F4: return "F4";
        case Key::F5: return "F5"; case Key::F6: return "F6";
        case Key::F7: return "F7"; case Key::F8: return "F8";
        case Key::F9: return "F9"; case Key::F10: return "F10";
        case Key::F11: return "F11"; case Key::F12: return "F12";
        case Key::F13: return "F13"; case Key::F14: return "F14";
        case Key::F15: return "F15"; case Key::F16: return "F16";
        case Key::F17: return "F17"; case Key::F18: return "F18";
        case Key::F19: return "F19"; case Key::F20: return "F20";
        case Key::F21: return "F21"; case Key::F22: return "F22";
        case Key::F23: return "F23"; case Key::F24: return "F24";

        case Key::Space: return "Space";
        case Key::Enter: return "Enter";
        case Key::Tab: return "Tab";
        case Key::Escape: return "Escape";
        case Key::Backspace: return "Backspace";
        case Key::Delete: return "Delete";
        case Key::Insert: return "Insert";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        case Key::Up: return "Up";
        case Key::Down: return "Down";
        case Key::Home: return "Home";
        case Key::End: return "End";
        case Key::PageUp: return "PageUp";
        case Key::PageDown: return "PageDown";

        case Key::Minus: return "Minus";
        case Key::Equal: return "Equal";
        case Key::LeftBracket: return "LeftBracket";
        case Key::RightBracket: return "RightBracket";
        case Key::Backslash: return "Backslash";
        case Key::Semicolon: return "Semicolon";
        case Key::Quote: return "Quote";
        case Key::Comma: return "Comma";
        case Key::Period: return "Period";
        case Key::Slash: return "Slash";
        case Key::Grave: return "Grave";

        case Key::LShift: return "LShift";
        case Key::RShift: return "RShift";
        case Key::LCtrl: return "LCtrl";
        case Key::RCtrl: return "RCtrl";
        case Key::LAlt: return "LAlt";
        case Key::RAlt: return "RAlt";
        case Key::LSuper: return "LSuper";
        case Key::RSuper: return "RSuper";

        case Key::Numpad0: return "Numpad0"; case Key::Numpad1: return "Numpad1";
        case Key::Numpad2: return "Numpad2"; case Key::Numpad3: return "Numpad3";
        case Key::Numpad4: return "Numpad4"; case Key::Numpad5: return "Numpad5";
        case Key::Numpad6: return "Numpad6"; case Key::Numpad7: return "Numpad7";
        case Key::Numpad8: return "Numpad8"; case Key::Numpad9: return "Numpad9";
        case Key::NumpadAdd: return "NumpadAdd";
        case Key::NumpadSubtract: return "NumpadSubtract";
        case Key::NumpadMultiply: return "NumpadMultiply";
        case Key::NumpadDivide: return "NumpadDivide";
        case Key::NumpadEnter: return "NumpadEnter";
        case Key::NumpadDecimal: return "NumpadDecimal";

        case Key::Count: return "Unknown";
    }
    return "Unknown";
}

constexpr bool isModifierKey(Key k) noexcept {
    switch (k) {
        case Key::LShift: case Key::RShift:
        case Key::LCtrl:  case Key::RCtrl:
        case Key::LAlt:   case Key::RAlt:
        case Key::LSuper: case Key::RSuper:
            return true;
        default:
            return false;
    }
}

}  // namespace threadmaxx::input::detail
