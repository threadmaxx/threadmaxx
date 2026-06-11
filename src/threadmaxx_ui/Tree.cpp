/// @file Tree.cpp
/// @brief Tree-node implementation. Open state stored in
/// `WidgetState::iv` per WidgetID.

#include "threadmaxx_ui/tree.hpp"

#include <cstdint>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace threadmaxx::ui {

namespace {

bool toggleOnClickOrKey(UIContext& ctx, const InteractionResult& r,
                        WidgetState& st) noexcept {
    bool changed = false;
    if (r.clicked) {
        st.iv = st.iv ? 0 : 1;
        changed = true;
    }
    const UIInput* in = ctx.input();
    if (r.focused && in) {
        if ((in->navKeysPressed & NavKey::Left) && st.iv != 0) {
            st.iv = 0;
            changed = true;
        }
        if ((in->navKeysPressed & NavKey::Right) && st.iv == 0) {
            st.iv = 1;
            changed = true;
        }
    }
    return changed;
}

} // namespace

bool treeNodeBegin(UIContext& ctx, WidgetID id, Rect bounds,
                   std::string_view label) noexcept {
    auto& st = ctx.widgetState(id);
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    (void)toggleOnClickOrKey(ctx, r, st);

    // Draw chevron + label.
    const std::int32_t chevron = 12;
    const Color chevColor = r.hovered ? theme::kAccent : theme::kText;
    if (st.iv) {
        // Down-pointing triangle ≈ "▼".
        ctx.drawList().emitLine(Vec2i{bounds.x + 2, bounds.y + 4},
                                Vec2i{bounds.x + 10, bounds.y + 4}, chevColor);
        ctx.drawList().emitLine(Vec2i{bounds.x + 2, bounds.y + 4},
                                Vec2i{bounds.x + 6, bounds.y + 10}, chevColor);
        ctx.drawList().emitLine(Vec2i{bounds.x + 10, bounds.y + 4},
                                Vec2i{bounds.x + 6, bounds.y + 10}, chevColor);
    } else {
        // Right-pointing triangle ≈ "▶".
        ctx.drawList().emitLine(Vec2i{bounds.x + 4, bounds.y + 2},
                                Vec2i{bounds.x + 4, bounds.y + 10}, chevColor);
        ctx.drawList().emitLine(Vec2i{bounds.x + 4, bounds.y + 2},
                                Vec2i{bounds.x + 10, bounds.y + 6}, chevColor);
        ctx.drawList().emitLine(Vec2i{bounds.x + 4, bounds.y + 10},
                                Vec2i{bounds.x + 10, bounds.y + 6}, chevColor);
    }
    ctx.drawList().emitText(Vec2i{bounds.x + chevron + 4, bounds.y + 2},
                            theme::kText, label);
    return st.iv != 0;
}

void treeNodeEnd(UIContext& /*ctx*/) noexcept {
    // Reserved: future indent-stack pop.
}

bool collapsingHeader(UIContext& ctx, WidgetID id, Rect bounds,
                      std::string_view label) noexcept {
    auto& st = ctx.widgetState(id);
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    (void)toggleOnClickOrKey(ctx, r, st);

    const Color bg = r.hovered ? theme::kPanelHover : theme::kPanel;
    ctx.drawList().emitRect(bounds, bg);
    ctx.drawList().emitText(Vec2i{bounds.x + 8, bounds.y + 2},
                            theme::kText, label);
    return st.iv != 0;
}

void setTreeOpen(UIContext& ctx, WidgetID id, bool open) noexcept {
    ctx.widgetState(id).iv = open ? 1 : 0;
}

bool isTreeOpen(const UIContext& ctx, WidgetID id) noexcept {
    const auto* st = ctx.tryGetWidgetState(id);
    return st && st->iv != 0;
}

} // namespace threadmaxx::ui
