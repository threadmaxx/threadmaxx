#pragma once

/// @file input.hpp
/// @brief Per-frame input snapshot fed to `UIContext::setInput()` + the
/// hit-test / focus / capture API. The UI library defines `UIInput` itself
/// so it doesn't depend on a future `threadmaxx_input` library; that
/// library, when it lands, will lower into this struct.
///
/// Coordinate system: pixel space, top-left origin, matches the draw stream
/// emitted by `draw.hpp`.

#include <array>
#include <cstdint>

#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

/// Modifier-key bit positions (bit-OR into `UIInput::modifiers`).
namespace Modifiers {
    inline constexpr std::uint16_t Shift = 1u << 0;
    inline constexpr std::uint16_t Ctrl  = 1u << 1;
    inline constexpr std::uint16_t Alt   = 1u << 2;
    inline constexpr std::uint16_t Super = 1u << 3;
}

/// Navigation key bit positions (bit-OR into `UIInput::navKeysPressed`).
/// Edge-triggered: a key is "pressed" only on the frame it transitions
/// down. Game code typically derives these from raw key events; UI
/// consumes the bitset.
namespace NavKey {
    inline constexpr std::uint16_t Tab      = 1u << 0;
    inline constexpr std::uint16_t ShiftTab = 1u << 1;
    inline constexpr std::uint16_t Enter    = 1u << 2;
    inline constexpr std::uint16_t Escape   = 1u << 3;
    inline constexpr std::uint16_t Left     = 1u << 4;
    inline constexpr std::uint16_t Right    = 1u << 5;
    inline constexpr std::uint16_t Up       = 1u << 6;
    inline constexpr std::uint16_t Down     = 1u << 7;
}

/// Mouse button bit positions (bit-OR into the mouse fields).
namespace MouseButton {
    inline constexpr std::uint8_t Left   = 1u << 0;
    inline constexpr std::uint8_t Right  = 1u << 1;
    inline constexpr std::uint8_t Middle = 1u << 2;
}

/// Maximum char codepoints the input ring buffer holds per frame. Anything
/// past this is silently dropped on the producer side.
inline constexpr std::size_t kMaxFrameChars = 32;

/// Per-frame input snapshot. Construct one in the host's input poll, hand
/// it to `UIContext::setInput()` before `beginFrame()`. Defaults are
/// "nothing happening this frame".
struct UIInput {
    /// Mouse pixel position; `{-1, -1}` = mouse outside the window.
    Vec2i mousePos{-1, -1};

    /// Mouse delta in pixels since the previous frame.
    Vec2i mouseDelta{};

    /// Scroll wheel delta in lines (positive = up).
    std::int32_t scrollY = 0;

    /// Which buttons are currently held down (any subset of `MouseButton::*`).
    std::uint8_t mouseButtons = 0;

    /// Edge: which buttons transitioned down THIS frame.
    std::uint8_t mouseButtonsPressed = 0;

    /// Edge: which buttons transitioned up THIS frame.
    std::uint8_t mouseButtonsReleased = 0;

    /// Modifier-key flag bits (bit-OR of `Modifiers::*`).
    std::uint16_t modifiers = 0;

    /// Navigation-key edges (bit-OR of `NavKey::*`).
    std::uint16_t navKeysPressed = 0;

    /// Character codepoints emitted this frame (typed text). UI3 only
    /// supports ASCII; UI4's text input consumes the ring.
    std::array<char, kMaxFrameChars> chars{};

    /// Number of valid entries in `chars`.
    std::uint8_t charsCount = 0;
};

/// Flags passed to `registerHitTest`.
namespace HitTestFlags {
    /// Default — visible only, not focusable, not keyboard-eligible.
    inline constexpr std::uint32_t None            = 0;
    /// Widget can be tabbed to and persists keyboard focus.
    inline constexpr std::uint32_t Focusable       = 1u << 0;
    /// While focused, the widget consumes keyboard input — host's
    /// `wantsKeyboardCapture()` returns true.
    inline constexpr std::uint32_t KeyboardCapture = 1u << 1;
    /// Widget should be skipped by hover resolution even though it has a
    /// bounds rect (useful for layout containers).
    inline constexpr std::uint32_t NoHover         = 1u << 2;
}

/// Outcome of `interact()` — the widget primitive in UI4 wraps this.
struct InteractionResult {
    bool hovered  = false;
    bool active   = false;
    bool focused  = false;
    bool clicked  = false;  // mouse-up inside, after a mouse-down inside
};

/// Register a hit-test region for `id` with `bounds` and `flags`. Walks the
/// hover / active / focused state and returns the result. Hover follows the
/// "last registered wins" rule: a later (= topmost-drawn) widget covering
/// the same pixel beats an earlier one.
InteractionResult interact(UIContext& ctx, WidgetID id, Rect bounds,
                           std::uint32_t flags = HitTestFlags::None) noexcept;

/// Force focus to a specific widget. The ID does not need to be currently
/// registered — focus survives until the next Tab edge.
void setFocus(UIContext& ctx, WidgetID id) noexcept;

/// Drop focus. Subsequent `isFocused(...)` returns false.
void clearFocus(UIContext& ctx) noexcept;

[[nodiscard]] bool isHovered(const UIContext& ctx, WidgetID id) noexcept;
[[nodiscard]] bool isActive(const UIContext& ctx, WidgetID id) noexcept;
[[nodiscard]] bool isFocused(const UIContext& ctx, WidgetID id) noexcept;

[[nodiscard]] WidgetID hoveredId(const UIContext& ctx) noexcept;
[[nodiscard]] WidgetID activeId(const UIContext& ctx) noexcept;
[[nodiscard]] WidgetID focusedId(const UIContext& ctx) noexcept;

/// True if the UI currently wants to consume mouse events (a widget is
/// active under the cursor).
[[nodiscard]] bool wantsMouseCapture(const UIContext& ctx) noexcept;

/// True if the UI currently wants to consume keyboard events (the focused
/// widget set `KeyboardCapture`).
[[nodiscard]] bool wantsKeyboardCapture(const UIContext& ctx) noexcept;

} // namespace threadmaxx::ui
