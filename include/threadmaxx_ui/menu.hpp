#pragma once

/// @file menu.hpp
/// @brief Menu bars + drop-down menus + popups. State is on `UIContext` —
/// one open popup at a time; an open menu bar latches so hovering siblings
/// switches the open submenu.
///
/// Patterns:
///
///   // Menu bar
///   beginMenuBar(ctx, barBounds);
///   if (beginMenu(ctx, fileId, fileBtnBounds, "File")) {
///       if (menuItem(ctx, openId, openBounds, "Open")) host->open();
///       endMenu(ctx);
///   }
///   endMenuBar(ctx);
///
///   // Standalone popup
///   if (mouseRightClickInBlankSpace) openPopup(ctx, ctxMenuId);
///   if (beginPopup(ctx, ctxMenuId, anchorBounds)) {
///       if (menuItem(ctx, ...)) ...
///       endPopup(ctx);
///   }

#include <cstdint>
#include <string_view>

#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

// -- Popups -------------------------------------------------------------------

/// Programmatically open the popup identified by `id`. Any previously open
/// popup is replaced.
void openPopup(UIContext& ctx, WidgetID id) noexcept;

/// Close any open popup.
void closePopup(UIContext& ctx) noexcept;

/// True if the popup `id` is the currently open one.
[[nodiscard]] bool isPopupOpen(const UIContext& ctx, WidgetID id) noexcept;

/// Begin a popup body. Returns true when `id` is open — caller MUST then
/// register the popup's widgets and finish with `endPopup`. Returns false
/// otherwise (skip the body entirely).
bool beginPopup(UIContext& ctx, WidgetID id, Rect bounds) noexcept;

/// Pair with a matching `beginPopup` that returned true.
void endPopup(UIContext& ctx) noexcept;

// -- Menu bar -----------------------------------------------------------------

/// Begin a menu bar region. Sets up the latching behavior for `beginMenu`
/// inside. Always pair with `endMenuBar`.
void beginMenuBar(UIContext& ctx, Rect bounds) noexcept;
void endMenuBar(UIContext& ctx) noexcept;

/// Top-level menu in the bar. Returns true if its submenu is open — caller
/// must then register submenu items and finish with `endMenu`. The visible
/// label rect is `bounds`. The submenu's items live below `bounds` (caller
/// places them).
bool beginMenu(UIContext& ctx, WidgetID id, Rect bounds,
               std::string_view label) noexcept;
void endMenu(UIContext& ctx) noexcept;

// -- Menu item ----------------------------------------------------------------

/// One item in a popup / submenu. Returns true on click (mouse release
/// inside, OR Enter while focused). On true, the enclosing popup closes.
bool menuItem(UIContext& ctx, WidgetID id, Rect bounds,
              std::string_view label) noexcept;

} // namespace threadmaxx::ui
