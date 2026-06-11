/// @file Widgets.cpp
/// @brief Implementation of the FR-2 widget set.

#include "threadmaxx_ui/widget.hpp"

#include <cstdint>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/input.hpp"

namespace threadmaxx::ui {

namespace {

Color buttonFillFor(bool hovered, bool active) noexcept {
    if (active)  return theme::kPanelActive;
    if (hovered) return theme::kPanelHover;
    return theme::kPanel;
}

float clampf(float v, float lo, float hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

} // namespace

// -- Static drawables ---------------------------------------------------------

void label(UIContext& ctx, Rect bounds, std::string_view text, Color color) noexcept {
    ctx.drawList().emitText(Vec2i{bounds.x, bounds.y}, color, text);
}

void separator(UIContext& ctx, Rect bounds, Color color) noexcept {
    const std::int32_t y = bounds.y + bounds.h / 2;
    ctx.drawList().emitLine(Vec2i{bounds.x, y},
                            Vec2i{bounds.x + bounds.w, y}, color, 1);
}

void imagePlaceholder(UIContext& ctx, Rect bounds,
                      std::uint32_t imageHandle, Color tint) noexcept {
    ctx.drawList().emitImage(bounds, imageHandle, tint);
}

// -- Button -------------------------------------------------------------------

bool button(UIContext& ctx, WidgetID id, Rect bounds,
            std::string_view labelText) noexcept {
    return button(ctx, id, bounds, labelText, ButtonStyle{});
}

bool button(UIContext& ctx, WidgetID id, Rect bounds,
            std::string_view labelText, ButtonStyle style) noexcept {
    InteractionResult r{};
    if (!style.disabled) {
        r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    } else {
        // Disabled buttons still occupy a hit-test slot but never activate.
        interact(ctx, id, bounds, HitTestFlags::NoHover);
    }
    const Color fill = style.disabled
        ? theme::kPanel
        : buttonFillFor(r.hovered, r.active);
    ctx.drawList().emitRect(bounds, fill);
    if (isFocused(ctx, id) && !style.disabled) {
        ctx.drawList().emitRect(bounds, theme::kPanelFocus, 1);
    }
    const Color textColor = style.disabled ? theme::kTextDisabled : theme::kText;
    ctx.drawList().emitText(Vec2i{bounds.x + 4, bounds.y + 2}, textColor, labelText);
    return r.clicked;
}

// -- Checkbox -----------------------------------------------------------------

bool checkbox(UIContext& ctx, WidgetID id, Rect bounds,
              std::string_view labelText, bool* value) noexcept {
    const std::int32_t box = bounds.h > 16 ? 16 : bounds.h;
    const Rect boxRect{bounds.x, bounds.y, box, box};
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    bool changed = false;
    if (r.clicked && value) {
        *value = !*value;
        changed = true;
    }
    const Color border = r.hovered ? theme::kAccent : theme::kBorder;
    ctx.drawList().emitRect(boxRect, theme::kPanel);
    ctx.drawList().emitRect(boxRect, border, 1);
    if (value && *value) {
        Rect tick{boxRect.x + 3, boxRect.y + 3,
                  boxRect.w - 6, boxRect.h - 6};
        ctx.drawList().emitRect(tick, theme::kAccent);
    }
    ctx.drawList().emitText(Vec2i{bounds.x + box + 6, bounds.y + 2},
                            theme::kText, labelText);
    return changed;
}

// -- Radio option -------------------------------------------------------------

bool radioOption(UIContext& ctx, WidgetID id, Rect bounds,
                 std::string_view labelText, std::int32_t* selected,
                 std::int32_t myValue) noexcept {
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    bool changed = false;
    if (r.clicked && selected && *selected != myValue) {
        *selected = myValue;
        changed = true;
    }
    const std::int32_t box = bounds.h > 14 ? 14 : bounds.h;
    const Rect boxRect{bounds.x, bounds.y, box, box};
    ctx.drawList().emitRect(boxRect, theme::kPanel);
    ctx.drawList().emitRect(boxRect, r.hovered ? theme::kAccent : theme::kBorder, 1);
    if (selected && *selected == myValue) {
        Rect dot{boxRect.x + 3, boxRect.y + 3,
                 boxRect.w - 6, boxRect.h - 6};
        ctx.drawList().emitRect(dot, theme::kAccent);
    }
    ctx.drawList().emitText(Vec2i{bounds.x + box + 6, bounds.y + 2},
                            theme::kText, labelText);
    return changed;
}

// -- Slider -------------------------------------------------------------------

bool slider(UIContext& ctx, WidgetID id, Rect bounds,
            float* value, float minV, float maxV) noexcept {
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    bool changed = false;
    const UIInput* in = ctx.input();
    if (value && r.active && in && bounds.w > 0) {
        const float t = static_cast<float>(in->mousePos.x - bounds.x)
                      / static_cast<float>(bounds.w);
        const float clampedT = clampf(t, 0.0f, 1.0f);
        const float newV = minV + (maxV - minV) * clampedT;
        if (newV != *value) {
            *value = newV;
            changed = true;
        }
    }
    // Track + fill + thumb.
    ctx.drawList().emitRect(bounds, theme::kPanel);
    if (value && maxV > minV) {
        const float t = clampf((*value - minV) / (maxV - minV), 0.0f, 1.0f);
        const std::int32_t fillW = static_cast<std::int32_t>(static_cast<float>(bounds.w) * t);
        Rect fill{bounds.x, bounds.y, fillW, bounds.h};
        ctx.drawList().emitRect(fill, theme::kAccent);
    }
    ctx.drawList().emitRect(bounds, r.hovered ? theme::kAccent : theme::kBorder, 1);
    return changed;
}

// -- Drag scalar --------------------------------------------------------------

bool dragScalar(UIContext& ctx, WidgetID id, Rect bounds,
                float* value, float speed) noexcept {
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    bool changed = false;
    const UIInput* in = ctx.input();
    if (value && r.active && in) {
        float scale = speed;
        if (in->modifiers & Modifiers::Ctrl)  scale *= 0.5f;
        if (in->modifiers & Modifiers::Shift) scale *= 2.0f;
        const float delta = static_cast<float>(in->mouseDelta.x) * scale;
        if (delta != 0.0f) {
            *value += delta;
            changed = true;
        }
    }
    ctx.drawList().emitRect(bounds, r.active ? theme::kPanelActive
                                              : r.hovered ? theme::kPanelHover
                                                          : theme::kPanel);
    return changed;
}

// -- Input text ---------------------------------------------------------------

bool inputText(UIContext& ctx, WidgetID id, Rect bounds,
               char* buf, std::size_t cap) noexcept {
    const auto r = interact(ctx, id, bounds,
                            HitTestFlags::Focusable | HitTestFlags::KeyboardCapture);
    bool committed = false;
    const UIInput* in = ctx.input();
    auto& st = ctx.widgetState(id);
    if (r.focused && in && buf && cap > 1) {
        std::size_t len = 0;
        while (len < cap - 1 && buf[len] != '\0') ++len;
        // Append typed chars.
        for (std::uint8_t i = 0; i < in->charsCount; ++i) {
            if (len + 1 >= cap) break;
            const char c = in->chars[i];
            if (c < 0x20) continue;  // skip control chars
            buf[len++] = c;
        }
        // Backspace edges trim one char.
        if (in->navKeysPressed & NavKey::Backspace) {
            if (len > 0) --len;
        }
        buf[len] = '\0';
        st.iv = static_cast<std::int64_t>(len);
        if (in->navKeysPressed & NavKey::Enter) {
            committed = true;
        }
    }
    Color border = theme::kBorder;
    if (r.focused) border = theme::kPanelFocus;
    else if (r.hovered) border = theme::kAccent;
    ctx.drawList().emitRect(bounds, theme::kPanel);
    ctx.drawList().emitRect(bounds, border, 1);
    if (buf) {
        ctx.drawList().emitText(Vec2i{bounds.x + 4, bounds.y + 2},
                                theme::kText,
                                std::string_view{buf, static_cast<std::size_t>(st.iv)});
    }
    return committed;
}

// -- Selectable ---------------------------------------------------------------

bool selectable(UIContext& ctx, WidgetID id, Rect bounds,
                std::string_view labelText, bool selected) noexcept {
    const auto r = interact(ctx, id, bounds, HitTestFlags::Focusable);
    Color bg = theme::kPanel;
    if (selected) bg = theme::kSelectionBg;
    else if (r.hovered) bg = theme::kPanelHover;
    if (r.active) bg = theme::kPanelActive;
    ctx.drawList().emitRect(bounds, bg);
    ctx.drawList().emitText(Vec2i{bounds.x + 4, bounds.y + 2},
                            theme::kText, labelText);
    return r.clicked;
}

// -- Tooltip ------------------------------------------------------------------

bool tooltip(UIContext& ctx, WidgetID host, Rect hostBounds,
             std::string_view text, float thresholdSeconds) noexcept {
    auto& st = ctx.widgetState(host);
    const UIInput* in = ctx.input();
    const bool hovered = isHovered(ctx, host);
    if (!hovered) {
        st.dv = 0.0;
        return false;
    }
    if (in) {
        st.dv += static_cast<double>(in->deltaTimeSeconds);
    }
    if (st.dv < static_cast<double>(thresholdSeconds)) return false;

    // Estimate label box: 7 px per char × len, 16 px tall. Position below
    // the host bounds; caller can override by positioning their own
    // tooltip if this doesn't fit.
    const std::int32_t w = static_cast<std::int32_t>(text.size()) * 7 + 8;
    const std::int32_t h = 16;
    Rect tipBounds{hostBounds.x, hostBounds.y + hostBounds.h + 2, w, h};
    ctx.drawList().emitRect(tipBounds, theme::kTooltipBg);
    ctx.drawList().emitText(Vec2i{tipBounds.x + 4, tipBounds.y + 2},
                            theme::kText, text);
    return true;
}

} // namespace threadmaxx::ui
