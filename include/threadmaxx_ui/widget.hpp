#pragma once

/// @file widget.hpp
/// @brief Primitive widgets — the FR-2 list from DESIGN_NOTES.md. Each entry
/// point takes a `WidgetID`, a `Rect`, and any value bindings; emits draw
/// commands into the active `UIContext`; returns a small status struct or
/// `bool` so call sites can branch.
///
/// Widgets here are stateless on entry except for what's bound to the
/// caller's variable; everything else (drag deltas, cursor positions,
/// hover-time counters) lives in `UIContext::widgetState(id)`.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

/// Default visual theme constants. Hard-coded for v1.0 — a `theme.hpp`
/// with push/pop slots lands in v1.x.
namespace theme {
inline constexpr Color kPanel        = rgba(0x2A2A2AFFu);
inline constexpr Color kPanelHover   = rgba(0x3A3A3AFFu);
inline constexpr Color kPanelActive  = rgba(0x4A4A4AFFu);
inline constexpr Color kPanelFocus   = rgba(0x4A6AAAFFu);
inline constexpr Color kAccent       = rgba(0x4A8AEEFFu);
inline constexpr Color kText         = rgba(0xE0E0E0FFu);
inline constexpr Color kTextDisabled = rgba(0x808080FFu);
inline constexpr Color kBorder       = rgba(0x404040FFu);
inline constexpr Color kSeparator    = rgba(0x303030FFu);
inline constexpr Color kSelectionBg  = rgba(0x2A4A8AFFu);
inline constexpr Color kTooltipBg    = rgba(0x101018F0u);
} // namespace theme

// -- Static drawables ---------------------------------------------------------

/// Emit a text label at `bounds.x, bounds.y` (top-left). No interaction.
void label(UIContext& ctx, Rect bounds, std::string_view text,
           Color color = theme::kText) noexcept;

/// Horizontal separator line one pixel tall at the vertical midpoint of
/// `bounds`. No interaction.
void separator(UIContext& ctx, Rect bounds,
               Color color = theme::kSeparator) noexcept;

/// Image placeholder — emits a textured-quad command with `imageHandle`.
/// No interaction at v1 (drag-source on an image is the host's call via
/// `dragdrop.hpp`).
void imagePlaceholder(UIContext& ctx, Rect bounds,
                      std::uint32_t imageHandle,
                      Color tint = Color{255, 255, 255, 255}) noexcept;

// -- Interactive --------------------------------------------------------------

/// Clickable button. Returns true on release-inside (the standard `clicked`
/// event from `interact()`).
bool button(UIContext& ctx, WidgetID id, Rect bounds,
            std::string_view label) noexcept;

/// Tristate parameter for `button` — call sites can mark a button disabled.
struct ButtonStyle {
    bool disabled = false;
};
bool button(UIContext& ctx, WidgetID id, Rect bounds,
            std::string_view label, ButtonStyle style) noexcept;

/// Boolean toggle. Mutates `*value` on click; returns true on the frame the
/// value changed.
bool checkbox(UIContext& ctx, WidgetID id, Rect bounds,
              std::string_view label, bool* value) noexcept;

/// Radio option — when clicked, sets `*selected = myValue`. Returns true on
/// the frame the value changed.
bool radioOption(UIContext& ctx, WidgetID id, Rect bounds,
                 std::string_view label, std::int32_t* selected,
                 std::int32_t myValue) noexcept;

/// Horizontal slider. Mouse drag inside `bounds` linearly maps to
/// `[minV, maxV]`. Click on the track snaps to the click position.
/// Returns true on the frame `*value` changed.
bool slider(UIContext& ctx, WidgetID id, Rect bounds,
            float* value, float minV, float maxV) noexcept;

/// Drag-to-edit scalar (no visible track). Drag delta scaled by `speed`
/// per pixel; `Ctrl` halves speed, `Shift` doubles it. Returns true on the
/// frame `*value` changed.
bool dragScalar(UIContext& ctx, WidgetID id, Rect bounds,
                float* value, float speed = 0.1f) noexcept;

/// Single-line text input. The host owns the `buf` storage (NUL-terminated;
/// `cap` includes the NUL). When focused, consumes UIInput chars and
/// backspace edges. Returns true on Enter (text committed).
bool inputText(UIContext& ctx, WidgetID id, Rect bounds,
               char* buf, std::size_t cap) noexcept;

/// Exclusive-selectable row (eg. one row in a list of files). `selected`
/// is the current selection state for display; returns true on click so
/// the caller can update its own selection state.
bool selectable(UIContext& ctx, WidgetID id, Rect bounds,
                std::string_view label, bool selected) noexcept;

/// Tooltip — call AFTER the host widget's `interact()`. When `host` has
/// been hovered for `thresholdSeconds`, emits a tooltip rect+text below
/// `hostBounds` and returns true.
bool tooltip(UIContext& ctx, WidgetID host, Rect hostBounds,
             std::string_view text,
             float thresholdSeconds = 0.5f) noexcept;

} // namespace threadmaxx::ui
