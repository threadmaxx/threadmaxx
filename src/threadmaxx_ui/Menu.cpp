/// @file Menu.cpp
/// @brief Menu bar + popup + menu item state machine.

#include "threadmaxx_ui/menu.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace threadmaxx::ui {

// -- Popups -------------------------------------------------------------------

void openPopup(UIContext& ctx, WidgetID id) noexcept {
    ctx.setPopupOpenId(id);
}

void closePopup(UIContext& ctx) noexcept {
    ctx.setPopupOpenId(WidgetID{});
    ctx.setMenuBarActive(false);
    ctx.setMenuBarOpenId(WidgetID{});
}

bool isPopupOpen(const UIContext& ctx, WidgetID id) noexcept {
    return ctx.popupOpenId() == id && id.value != 0;
}

bool beginPopup(UIContext& ctx, WidgetID id, Rect bounds) noexcept {
    if (!isPopupOpen(ctx, id)) return false;
    ctx.pushPopupBody();
    // Background. The popup body's widgets register normally; because the
    // popup is open, sibling widgets outside have already been shadowed.
    ctx.drawList().emitRect(bounds, theme::kPanel);
    ctx.drawList().emitRect(bounds, theme::kBorder, 1);
    return true;
}

void endPopup(UIContext& ctx) noexcept {
    ctx.popPopupBody();
}

// -- Menu bar -----------------------------------------------------------------

void beginMenuBar(UIContext& ctx, Rect bounds) noexcept {
    ctx.drawList().emitRect(bounds, theme::kPanel);
}

void endMenuBar(UIContext& /*ctx*/) noexcept {
    // Reserved for future styling.
}

bool beginMenu(UIContext& ctx, WidgetID id, Rect bounds,
               std::string_view labelText) noexcept {
    const auto r = interact(ctx, id, bounds,
                            HitTestFlags::Focusable |
                            HitTestFlags::BypassPopupShadow);
    const Color fill = r.hovered ? theme::kPanelHover : theme::kPanel;
    ctx.drawList().emitRect(bounds, fill);
    ctx.drawList().emitText(Vec2i{bounds.x + 6, bounds.y + 2},
                            theme::kText, labelText);

    // Click activates the menu bar with this menu open. Hovering a
    // different menu while the bar is active switches without needing
    // another click.
    bool open = ctx.menuBarOpenId() == id && ctx.menuBarActive();
    if (r.clicked) {
        if (open) {
            // Same menu clicked again → toggle off.
            closePopup(ctx);
            open = false;
        } else {
            ctx.setMenuBarActive(true);
            ctx.setMenuBarOpenId(id);
            openPopup(ctx, id);
            open = true;
        }
    } else if (r.hovered && ctx.menuBarActive() && ctx.menuBarOpenId() != id) {
        ctx.setMenuBarOpenId(id);
        openPopup(ctx, id);
        open = true;
    }

    if (open) ctx.pushPopupBody();
    return open;
}

void endMenu(UIContext& ctx) noexcept {
    if (ctx.popupBodyDepth() > 0) ctx.popPopupBody();
}

// -- Menu item ----------------------------------------------------------------

bool menuItem(UIContext& ctx, WidgetID id, Rect bounds,
              std::string_view labelText) noexcept {
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    const Color bg = r.hovered ? theme::kPanelHover : theme::kPanel;
    ctx.drawList().emitRect(bounds, bg);
    ctx.drawList().emitText(Vec2i{bounds.x + 6, bounds.y + 2},
                            theme::kText, labelText);

    bool fired = r.clicked;
    const UIInput* in = ctx.input();
    if (!fired && r.focused && in && (in->navKeysPressed & NavKey::Enter)) {
        fired = true;
    }
    if (fired) closePopup(ctx);
    return fired;
}

} // namespace threadmaxx::ui
