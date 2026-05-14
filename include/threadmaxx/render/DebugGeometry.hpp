#pragma once

#include "../Components.hpp"  // Vec3

#include <cstdint>
#include <string_view>

namespace threadmaxx {

/// One world-space debug line segment, packed RGBA color.
struct DebugLine {
    Vec3 a;
    Vec3 b;
    /// Packed `0xAABBGGRR`. The renderer is free to convert to its
    /// native color space.
    std::uint32_t colorRGBA = 0xFFFFFFFFu;
};

/// One world-space debug point with on-screen pixel size.
struct DebugPoint {
    Vec3 position;
    std::uint32_t colorRGBA = 0xFFFFFFFFu;
    /// Pixel radius hint; renderers may round / clamp.
    float pixelSize = 4.0f;
};

/// One piece of world-anchored debug text. The string is borrowed from
/// the producer; lifetime must extend to the next render frame swap
/// (i.e. the producer holds the storage until the renderer has
/// consumed the frame).
///
/// In practice the producer stores text in a ring buffer keyed to the
/// frame's tick and reclaims storage two frames later — the same
/// pattern as @ref AnimationPoseRef::ringSlot.
struct DebugText {
    Vec3 position;
    std::string_view text;
    std::uint32_t colorRGBA = 0xFFFFFFFFu;
};

} // namespace threadmaxx
