/// @file Gizmo.cpp
/// @brief Screen-space 2D drag handle.

#include "threadmaxx_ui/gizmo.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace threadmaxx::ui {

GizmoEvent dragHandle2D(UIContext& ctx, WidgetID id, Rect bounds) noexcept {
    const auto r = interact(ctx, id, bounds);
    GizmoEvent ev{};
    ev.hovered = r.hovered;
    ev.active = r.active;
    const UIInput* in = ctx.input();
    if (r.active && in && (in->mouseDelta.x != 0 || in->mouseDelta.y != 0)) {
        ev.dragging = true;
        ev.delta = in->mouseDelta;
    }
    // Visual.
    Color c = theme::kBorder;
    if (r.active)        c = theme::kAccent;
    else if (r.hovered)  c = theme::kPanelHover;
    ctx.drawList().emitRect(bounds, c);
    return ev;
}

} // namespace threadmaxx::ui
