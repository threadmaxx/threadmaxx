#pragma once

/// @file types.hpp
/// @brief Foundational POD types — IDs, rects, colors, vectors.
///
/// Every type here is trivially copyable, no allocations, no virtuals.
/// Larger structures (`UIContext`, `DrawList`) compose these.

#include <cstdint>

namespace threadmaxx::ui {

/// Stable identifier for a widget within a frame. Derived from the current
/// ID stack via FNV-1a-64 hashing; matches across frames as long as the
/// caller's `pushId(...)` path is the same. Two distinct paths producing
/// the same hash is a collision: rare for 64-bit FNV-1a, but possible.
struct WidgetID {
    std::uint64_t value = 0;
};

inline constexpr bool operator==(WidgetID a, WidgetID b) noexcept {
    return a.value == b.value;
}
inline constexpr bool operator!=(WidgetID a, WidgetID b) noexcept {
    return !(a == b);
}

/// 2D integer vector (pixel coordinates inside the draw stream).
struct Vec2i {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

inline constexpr bool operator==(Vec2i a, Vec2i b) noexcept {
    return a.x == b.x && a.y == b.y;
}
inline constexpr bool operator!=(Vec2i a, Vec2i b) noexcept {
    return !(a == b);
}

/// Axis-aligned rectangle in pixel space (top-left origin).
/// `w` / `h` are widths in pixels; zero-sized rects are valid (and empty).
struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
};

inline constexpr bool operator==(Rect a, Rect b) noexcept {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
inline constexpr bool operator!=(Rect a, Rect b) noexcept {
    return !(a == b);
}

/// True if `r` has zero area.
[[nodiscard]] inline constexpr bool isEmpty(Rect r) noexcept {
    return r.w <= 0 || r.h <= 0;
}

/// True if pixel `(px, py)` is inside `r` (top-left inclusive, bottom-right
/// exclusive).
[[nodiscard]] inline constexpr bool contains(Rect r, std::int32_t px, std::int32_t py) noexcept {
    return px >= r.x && px < r.x + r.w &&
           py >= r.y && py < r.y + r.h;
}

/// Intersection of two rects. Returns an empty rect (`w` or `h` ≤ 0) when
/// they don't overlap.
[[nodiscard]] inline constexpr Rect intersect(Rect a, Rect b) noexcept {
    const std::int32_t x0 = a.x > b.x ? a.x : b.x;
    const std::int32_t y0 = a.y > b.y ? a.y : b.y;
    const std::int32_t x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const std::int32_t y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    Rect out{};
    out.x = x0;
    out.y = y0;
    out.w = x1 > x0 ? x1 - x0 : 0;
    out.h = y1 > y0 ? y1 - y0 : 0;
    return out;
}

/// Smallest rect that contains both `a` and `b`. Treats empty rects as the
/// identity (union with empty = the other rect).
[[nodiscard]] inline constexpr Rect unionRect(Rect a, Rect b) noexcept {
    if (isEmpty(a)) return b;
    if (isEmpty(b)) return a;
    const std::int32_t x0 = a.x < b.x ? a.x : b.x;
    const std::int32_t y0 = a.y < b.y ? a.y : b.y;
    const std::int32_t x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const std::int32_t y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    Rect out{};
    out.x = x0;
    out.y = y0;
    out.w = x1 - x0;
    out.h = y1 - y0;
    return out;
}

/// 8-bit-per-channel RGBA color, premultiplied alpha by convention.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

inline constexpr bool operator==(Color a, Color b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
inline constexpr bool operator!=(Color a, Color b) noexcept {
    return !(a == b);
}

/// Construct a color from a packed `0xRRGGBBAA` literal — useful for
/// theme constants.
[[nodiscard]] inline constexpr Color rgba(std::uint32_t v) noexcept {
    return Color{
        static_cast<std::uint8_t>((v >> 24) & 0xFFu),
        static_cast<std::uint8_t>((v >> 16) & 0xFFu),
        static_cast<std::uint8_t>((v >>  8) & 0xFFu),
        static_cast<std::uint8_t>( v        & 0xFFu)};
}

} // namespace threadmaxx::ui
