/// @file Panel.cpp
/// @brief Panel windows with movable title bar + corner resize handle.
/// Bounds are clamped to the host rect on every drag step.

#include "threadmaxx_ui/panel.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/gizmo.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace threadmaxx::ui {

namespace {

Rect clampToHost(Rect r, Rect host) noexcept {
    if (r.x < host.x) r.x = host.x;
    if (r.y < host.y) r.y = host.y;
    if (r.x + r.w > host.x + host.w) r.x = host.x + host.w - r.w;
    if (r.y + r.h > host.y + host.h) r.y = host.y + host.h - r.h;
    return r;
}

constexpr std::int64_t kDoubleClickFrameWindow = 20;

} // namespace

Rect hostRect(const UIContext& ctx) noexcept { return ctx.hostRect(); }
void setHostRect(UIContext& ctx, Rect r) noexcept { ctx.setHostRect(r); }

bool beginPanel(UIContext& ctx, WidgetID id, std::string_view title,
                PanelState& state) noexcept {
    const WidgetID titleId{id.value ^ 0x100ULL};
    const WidgetID resizeId{id.value ^ 0x200ULL};

    // Title bar = top kPanelTitleHeight px of bounds.
    const Rect tb{state.bounds.x, state.bounds.y, state.bounds.w, kPanelTitleHeight};
    const auto rTb = interact(ctx, titleId, tb, HitTestFlags::Focusable);
    const UIInput* in = ctx.input();
    if (rTb.active && in && (in->mouseDelta.x != 0 || in->mouseDelta.y != 0)) {
        state.bounds.x += in->mouseDelta.x;
        state.bounds.y += in->mouseDelta.y;
        state.bounds = clampToHost(state.bounds, ctx.hostRect());
    }
    // Double-click detection via the WidgetState integer slot:
    //   iv = frameCount of last click (0 = never).
    if (rTb.clicked) {
        auto& st = ctx.widgetState(titleId);
        const std::int64_t prevClickFrame = st.iv;
        const std::int64_t now = static_cast<std::int64_t>(ctx.frameCount());
        if (prevClickFrame > 0 && (now - prevClickFrame) <= kDoubleClickFrameWindow) {
            state.collapsed = !state.collapsed;
            st.iv = 0;
        } else {
            st.iv = now;
        }
    }

    // Draw title bar + title text.
    const Color tbFill = rTb.hovered ? theme::kPanelHover : theme::kPanel;
    ctx.drawList().emitRect(tb, tbFill);
    ctx.drawList().emitText(Vec2i{tb.x + 6, tb.y + 4}, theme::kText, title);

    if (state.collapsed) {
        ctx.drawList().emitRect(tb, theme::kBorder, 1);
        return false;
    }

    // Body background.
    const Rect body{state.bounds.x, state.bounds.y + kPanelTitleHeight,
                    state.bounds.w, state.bounds.h - kPanelTitleHeight};
    ctx.drawList().emitRect(body, theme::kPanel);
    ctx.drawList().emitRect(state.bounds, theme::kBorder, 1);

    // Resize handle (bottom-right).
    const Rect rh{state.bounds.x + state.bounds.w - kPanelResizeHandle,
                  state.bounds.y + state.bounds.h - kPanelResizeHandle,
                  kPanelResizeHandle, kPanelResizeHandle};
    const auto rRh = dragHandle2D(ctx, resizeId, rh);
    if (rRh.dragging) {
        state.bounds.w += rRh.delta.x;
        state.bounds.h += rRh.delta.y;
        if (state.bounds.w < state.minSize.x) state.bounds.w = state.minSize.x;
        if (state.bounds.h < state.minSize.y) state.bounds.h = state.minSize.y;
        state.bounds = clampToHost(state.bounds, ctx.hostRect());
    }

    return true;
}

void endPanel(UIContext& /*ctx*/) noexcept {
    // Reserved for future use (clip pop / layout pop).
}

} // namespace threadmaxx::ui
