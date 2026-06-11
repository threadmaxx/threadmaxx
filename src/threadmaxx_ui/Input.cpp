/// @file Input.cpp
/// @brief Implementation of the input / hit-test / focus state machine.
/// Hover resolves as "last registered wins" (= topmost in draw order);
/// active is sticky from mouse-down to mouse-up; Tab focus advances at
/// `endFrame()` based on the focusable subset registered this frame.

#include "threadmaxx_ui/input.hpp"

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

namespace {

[[nodiscard]] bool mouseInside(const UIInput* input, Rect bounds) noexcept {
    if (!input) return false;
    return contains(bounds, input->mousePos.x, input->mousePos.y);
}

[[nodiscard]] bool leftPressed(const UIInput* input) noexcept {
    return input && (input->mouseButtonsPressed & MouseButton::Left) != 0;
}

[[nodiscard]] bool leftReleased(const UIInput* input) noexcept {
    return input && (input->mouseButtonsReleased & MouseButton::Left) != 0;
}

} // namespace

InteractionResult interact(UIContext& ctx, WidgetID id, Rect bounds,
                           std::uint32_t flags) noexcept {
    // Record the hit test so `endFrame()` can run focus / active cleanup.
    HitTestRecord rec{};
    rec.id = id;
    rec.bounds = bounds;
    rec.flags = flags;
    ctx.hitTests().push_back(rec);

    if (flags & HitTestFlags::Focusable) {
        ctx.bumpFocusableCount();
    }

    const UIInput* input = ctx.input();
    const bool insideCursor = (flags & HitTestFlags::NoHover) == 0 &&
                              mouseInside(input, bounds);

    if (insideCursor) {
        ctx.setHoveredId(id);  // last-registered-wins resolution
    }

    InteractionResult r{};
    r.hovered = (ctx.hoveredId() == id);

    // Active state transition.
    if (r.hovered && leftPressed(input) && ctx.activeId() != id) {
        ctx.setActiveId(id);
        if (flags & HitTestFlags::Focusable) {
            ctx.setFocusedId(id);
        }
    }

    if (ctx.activeId() == id) {
        if (leftReleased(input)) {
            // Click event fires only on release inside the bounds.
            if (insideCursor) r.clicked = true;
            ctx.setActiveId(WidgetID{});
        }
        r.active = true;
    }

    r.focused = (ctx.focusedId() == id) && id.value != 0;
    if (r.focused && (flags & HitTestFlags::KeyboardCapture)) {
        ctx.setFocusKeyboardCapture(true);
    }
    return r;
}

void setFocus(UIContext& ctx, WidgetID id) noexcept {
    ctx.setFocusedId(id);
}

void clearFocus(UIContext& ctx) noexcept {
    ctx.setFocusedId(WidgetID{});
}

bool isHovered(const UIContext& ctx, WidgetID id) noexcept {
    return ctx.hoveredId() == id && id.value != 0;
}

bool isActive(const UIContext& ctx, WidgetID id) noexcept {
    return ctx.activeId() == id && id.value != 0;
}

bool isFocused(const UIContext& ctx, WidgetID id) noexcept {
    return ctx.focusedId() == id && id.value != 0;
}

WidgetID hoveredId(const UIContext& ctx) noexcept { return ctx.hoveredId(); }
WidgetID activeId(const UIContext& ctx)  noexcept { return ctx.activeId();  }
WidgetID focusedId(const UIContext& ctx) noexcept { return ctx.focusedId(); }

bool wantsMouseCapture(const UIContext& ctx) noexcept {
    return ctx.activeId().value != 0;
}

bool wantsKeyboardCapture(const UIContext& ctx) noexcept {
    return ctx.focusKeyboardCapture();
}

// -- UIContext::finalizeInputState ---------------------------------------------

void UIContext::finalizeInputState() noexcept {
    const UIInput* in = input_;

    // 1) Drop active state if its widget didn't register this frame.
    if (activeId_.value != 0) {
        bool stillRegistered = false;
        for (const auto& rec : hitTests_) {
            if (rec.id == activeId_) { stillRegistered = true; break; }
        }
        if (!stillRegistered) activeId_ = WidgetID{};
    }

    // 2) Tab navigation — process Tab / Shift-Tab edges against the
    //    focusables registered this frame.
    if (!in) return;

    const bool tab      = (in->navKeysPressed & NavKey::Tab) != 0;
    const bool shiftTab = (in->navKeysPressed & NavKey::ShiftTab) != 0
                       || (tab && (in->modifiers & Modifiers::Shift) != 0);

    if (!tab && !shiftTab) return;

    // Build the focusables list in registration order.
    std::int32_t focusedIdx = -1;
    std::int32_t focusables[64];
    std::int32_t focusableN = 0;
    for (std::size_t i = 0; i < hitTests_.size(); ++i) {
        const auto& rec = hitTests_[i];
        if ((rec.flags & HitTestFlags::Focusable) == 0) continue;
        if (focusableN < 64) {
            focusables[focusableN] = static_cast<std::int32_t>(i);
        }
        if (rec.id == focusedId_) {
            focusedIdx = focusableN;
        }
        ++focusableN;
    }
    if (focusableN == 0) return;
    const std::int32_t cappedN = focusableN < 64 ? focusableN : 64;

    std::int32_t target = 0;
    if (shiftTab) {
        target = (focusedIdx < 0) ? cappedN - 1 : (focusedIdx - 1 + cappedN) % cappedN;
    } else {
        target = (focusedIdx < 0) ? 0 : (focusedIdx + 1) % cappedN;
    }
    focusedId_ = hitTests_[static_cast<std::size_t>(focusables[target])].id;
}

} // namespace threadmaxx::ui
