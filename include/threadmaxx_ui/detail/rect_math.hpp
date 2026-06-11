#pragma once

/// @file detail/rect_math.hpp
/// @brief Rect helpers that don't belong in the public `types.hpp` — splits,
/// margins, signed offsets. Layout (UI2) consumes these heavily; UI1 only
/// uses `inset` for the no-alloc-test scaffolding.

#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui::detail {

/// Shrinks a rect uniformly on all four sides. Negative `margin` grows.
/// Result clamps to a zero-area rect rather than going negative.
[[nodiscard]] inline constexpr Rect inset(Rect r, std::int32_t margin) noexcept {
    Rect out{};
    out.x = r.x + margin;
    out.y = r.y + margin;
    const std::int32_t w = r.w - 2 * margin;
    const std::int32_t h = r.h - 2 * margin;
    out.w = w > 0 ? w : 0;
    out.h = h > 0 ? h : 0;
    return out;
}

/// Translates a rect by `(dx, dy)`.
[[nodiscard]] inline constexpr Rect translate(Rect r, std::int32_t dx, std::int32_t dy) noexcept {
    Rect out{};
    out.x = r.x + dx;
    out.y = r.y + dy;
    out.w = r.w;
    out.h = r.h;
    return out;
}

} // namespace threadmaxx::ui::detail
