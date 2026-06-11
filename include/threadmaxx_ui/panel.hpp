#pragma once

/// @file panel.hpp
/// @brief Movable + resizable panel windows. The host owns a `PanelState`
/// per panel; the library mutates `bounds` on drag and toggles `collapsed`
/// on a double-click of the title bar.
///
/// Pattern:
///
///   static PanelState ps{ .bounds = {100, 100, 320, 240} };
///   if (beginPanel(ctx, id, "Properties", ps)) {
///       // body
///       endPanel(ctx);
///   }

#include <cstdint>
#include <string_view>

#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

/// Host-owned panel state. The library mutates `bounds` and `collapsed`;
/// everything else is read-only configuration.
struct PanelState {
    Rect bounds{};
    bool collapsed = false;
    Vec2i minSize{120, 60};
};

/// Begin a panel. Returns true if the body is visible (not collapsed) —
/// caller must register body widgets then call `endPanel`. Returns false
/// when collapsed (skip the body); `endPanel` MUST still NOT be called.
bool beginPanel(UIContext& ctx, WidgetID id, std::string_view title,
                PanelState& state) noexcept;

/// Pair with a `beginPanel` that returned true.
void endPanel(UIContext& ctx) noexcept;

/// Height of the title bar in pixels.
inline constexpr std::int32_t kPanelTitleHeight = 22;

/// Size of the corner resize handle in pixels.
inline constexpr std::int32_t kPanelResizeHandle = 12;

/// Window inside which the panel's bounds clamp. Defaults to a large
/// virtual rect; `setHostRect` lets the host shrink it.
[[nodiscard]] Rect hostRect(const UIContext& ctx) noexcept;
void setHostRect(UIContext& ctx, Rect r) noexcept;

} // namespace threadmaxx::ui
