#pragma once

/// @file layout.hpp
/// @brief Row / column / nested-region helpers + the size-resolution math.
///
/// Two layers:
///
///   1. Pure resolution functions (`resolveRow`, `resolveColumn`). Given a
///      parent rect, a span of `Size` requests, padding, and spacing, they
///      fill an output span of `Rect`s. Stateless, testable in isolation.
///
///   2. A scoped stack on the `UIContext` (`pushLayout` / `popLayout`)
///      for nested authoring. Widgets emitted inside a layout frame can
///      query `currentLayout(ctx)` for the active content rect.
///
/// `Size` is either fixed pixels or a flex weight. Flex children share the
/// content rect's remaining budget after fixed siblings and spacing are
/// subtracted; the last flex child absorbs any rounding leftover so the
/// resolved rects are gap-free.

#include <cstdint>
#include <span>

#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

/// How a child requests size along the layout's primary axis.
enum class SizeMode : std::uint8_t {
    Fixed = 0,
    Flex  = 1,
};

/// One child's size request.
struct Size {
    SizeMode mode       = SizeMode::Fixed;
    std::int32_t pixels = 0;
    float flexWeight    = 0.0f;

    [[nodiscard]] static constexpr Size fixed(std::int32_t px) noexcept {
        Size s{};
        s.mode = SizeMode::Fixed;
        s.pixels = px < 0 ? 0 : px;
        return s;
    }
    [[nodiscard]] static constexpr Size flex(float weight = 1.0f) noexcept {
        Size s{};
        s.mode = SizeMode::Flex;
        s.flexWeight = weight > 0.0f ? weight : 0.0f;
        return s;
    }
};

/// Returns the rect inside `parent` after `padding` has been subtracted on
/// all four sides. Clamps to a zero-area rect rather than going negative.
[[nodiscard]] inline constexpr Rect applyPadding(Rect parent, Padding p) noexcept {
    Rect out{};
    out.x = parent.x + p.left;
    out.y = parent.y + p.top;
    const std::int32_t w = parent.w - p.left - p.right;
    const std::int32_t h = parent.h - p.top  - p.bottom;
    out.w = w > 0 ? w : 0;
    out.h = h > 0 ? h : 0;
    return out;
}

/// Resolve a row of children into `out`. `out.size()` MUST equal
/// `sizes.size()`; extra slots are ignored, missing slots truncate.
/// Fixed siblings consume their pixels exactly; flex siblings share the
/// remaining width proportional to their flex weights. The last flex
/// sibling absorbs the integer rounding leftover so no gap appears between
/// adjacent siblings.
void resolveRow(Rect parent, std::span<const Size> sizes, std::span<Rect> out,
                Padding pad = {}, std::int32_t spacing = 0) noexcept;

/// Column counterpart — sizes resolve along the Y axis.
void resolveColumn(Rect parent, std::span<const Size> sizes, std::span<Rect> out,
                   Padding pad = {}, std::int32_t spacing = 0) noexcept;

// -- Scoped layout stack on UIContext -----------------------------------------

/// Push a new layout frame onto the context's stack. `bounds` is the outer
/// rect; padding is applied to derive `content`. Returns the index of the
/// pushed frame, or `kLayoutOverflowed` (= -1) if the stack was already at
/// `kLayoutStackDepth`.
std::int32_t pushLayout(UIContext& ctx, Rect bounds, Orientation orient,
                        Padding pad = {}, std::int32_t spacing = 0) noexcept;

/// Pop the topmost layout frame. No-op if the stack is empty.
void popLayout(UIContext& ctx) noexcept;

/// Returns the topmost layout frame. Caller is responsible for ensuring the
/// stack is non-empty (asserts in debug).
[[nodiscard]] const LayoutFrame& currentLayout(const UIContext& ctx) noexcept;

/// Push a clip rect (intersected with the parent clip) onto the clip stack
/// AND emit a `ClipPush` draw command. Returns the effective (intersected)
/// rect that the backend will use.
Rect pushClip(UIContext& ctx, Rect bounds) noexcept;

/// Pop the topmost clip rect AND emit a `ClipPop` draw command.
void popClip(UIContext& ctx) noexcept;

/// Sentinel returned by `pushLayout` on overflow.
inline constexpr std::int32_t kLayoutOverflowed = -1;

} // namespace threadmaxx::ui
