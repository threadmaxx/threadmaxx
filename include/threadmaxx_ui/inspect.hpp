#pragma once

/// @file inspect.hpp
/// @brief Property-inspector rows. Each `inspect(ctx, id, bounds, label,
/// value)` overload renders a label on the left half of `bounds` and an
/// editable control on the right half; returns true on the frame the
/// value changed.
///
/// Coverage at v1: bool / int / float / string (char buffer) / enum (cycle
/// through caller-provided options) / vec3 (three floats laid out in a row)
/// / read-only handle. Reflection-driven inspection is a v1.x addition once
/// `threadmaxx_reflect` lands.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/draw.hpp"
#include "threadmaxx_ui/types.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace threadmaxx::ui {

namespace detail {

/// Splits a row rect into label rect (left ~40%) and value rect (right).
inline std::pair<Rect, Rect> splitRow(Rect row, float splitFraction = 0.4f) noexcept {
    if (row.w <= 0 || row.h <= 0) return {row, row};
    const std::int32_t labelW = static_cast<std::int32_t>(static_cast<float>(row.w) * splitFraction);
    const Rect labelRect{row.x, row.y, labelW, row.h};
    const Rect valueRect{row.x + labelW + 4, row.y,
                         row.w - labelW - 4, row.h};
    return {labelRect, valueRect};
}

inline void emitRowLabel(UIContext& ctx, Rect labelRect, std::string_view label) noexcept {
    ctx.drawList().emitText(Vec2i{labelRect.x, labelRect.y + 2},
                            theme::kText, label);
}

/// Formats `*value` into the value rect. Out-of-line so GCC doesn't blow up
/// inlining snprintf into every test TU with bogus -Wstringop-overflow
/// warnings.
void renderIntValue(UIContext& ctx, Rect valueRect, std::int32_t value) noexcept;
void renderFloatValue(UIContext& ctx, Rect valueRect, float value) noexcept;
void renderHandleValue(UIContext& ctx, Rect valueRect, std::uint64_t v) noexcept;

} // namespace detail

/// Bool — renders as a checkbox.
inline bool inspect(UIContext& ctx, WidgetID id, Rect row,
                    std::string_view label, bool* value) noexcept {
    const auto [lr, vr] = detail::splitRow(row);
    detail::emitRowLabel(ctx, lr, label);
    return checkbox(ctx, id, vr, "", value);
}

/// 32-bit signed int — drag scalar with integer snap.
inline bool inspect(UIContext& ctx, WidgetID id, Rect row,
                    std::string_view label, std::int32_t* value) noexcept {
    const auto [lr, vr] = detail::splitRow(row);
    detail::emitRowLabel(ctx, lr, label);
    if (!value) {
        ctx.drawList().emitRect(vr, theme::kPanel);
        return false;
    }
    auto& st = ctx.widgetState(id);
    if (st.lastSeenFrame == 0) st.dv = static_cast<double>(*value);
    float f = static_cast<float>(st.dv);
    const bool changed = dragScalar(ctx, id, vr, &f, 0.25f);
    st.dv = static_cast<double>(f);
    const std::int32_t snapped = static_cast<std::int32_t>(f);
    if (snapped != *value) *value = snapped;
    detail::renderIntValue(ctx, vr, *value);
    return changed;
}

/// Float — drag scalar with the bound value shown numerically.
inline bool inspect(UIContext& ctx, WidgetID id, Rect row,
                    std::string_view label, float* value) noexcept {
    const auto [lr, vr] = detail::splitRow(row);
    detail::emitRowLabel(ctx, lr, label);
    const bool changed = dragScalar(ctx, id, vr, value, 0.1f);
    if (value) detail::renderFloatValue(ctx, vr, *value);
    return changed;
}

/// String — single-line text input.
inline bool inspect(UIContext& ctx, WidgetID id, Rect row,
                    std::string_view label, char* buf, std::size_t cap) noexcept {
    const auto [lr, vr] = detail::splitRow(row);
    detail::emitRowLabel(ctx, lr, label);
    return inputText(ctx, id, vr, buf, cap);
}

/// Three floats laid out as a horizontal triplet (XYZ vector / RGB / size).
inline bool inspect(UIContext& ctx, WidgetID id, Rect row,
                    std::string_view label,
                    float* x, float* y, float* z) noexcept {
    const auto [lr, vr] = detail::splitRow(row);
    detail::emitRowLabel(ctx, lr, label);
    const std::int32_t compW = (vr.w - 4) / 3;
    const Rect xR{vr.x,                vr.y, compW, vr.h};
    const Rect yR{vr.x + compW + 2,    vr.y, compW, vr.h};
    const Rect zR{vr.x + (compW + 2)*2, vr.y, compW, vr.h};

    auto& st = ctx.widgetState(id);
    const std::uint64_t base = id.value;
    const bool cx = dragScalar(ctx, WidgetID{base ^ 0x1ULL}, xR, x, 0.1f);
    const bool cy = dragScalar(ctx, WidgetID{base ^ 0x2ULL}, yR, y, 0.1f);
    const bool cz = dragScalar(ctx, WidgetID{base ^ 0x3ULL}, zR, z, 0.1f);
    st.lastSeenFrame = ctx.frameCount();
    return cx || cy || cz;
}

/// Enum dropdown via click-to-cycle.
template <class E>
bool inspectEnum(UIContext& ctx, WidgetID id, Rect row,
                 std::string_view label,
                 E* value,
                 std::span<const std::pair<E, std::string_view>> options) noexcept {
    const auto [lr, vr] = detail::splitRow(row);
    detail::emitRowLabel(ctx, lr, label);
    if (options.empty() || !value) {
        ctx.drawList().emitRect(vr, theme::kPanel);
        return false;
    }
    std::size_t idx = 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].first == *value) { idx = i; break; }
    }
    const bool clicked = button(ctx, id, vr, options[idx].second);
    if (clicked) {
        idx = (idx + 1) % options.size();
        *value = options[idx].first;
        return true;
    }
    return false;
}

/// Read-only handle row — displays the numeric value as a label.
inline bool inspect(UIContext& ctx, WidgetID /*id*/, Rect row,
                    std::string_view label, std::uint64_t handleValue) noexcept {
    const auto [lr, vr] = detail::splitRow(row);
    detail::emitRowLabel(ctx, lr, label);
    ctx.drawList().emitRect(vr, theme::kPanel);
    detail::renderHandleValue(ctx, vr, handleValue);
    return false;
}

} // namespace threadmaxx::ui
