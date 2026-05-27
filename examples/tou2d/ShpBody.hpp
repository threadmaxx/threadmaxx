#pragma once

// Header-only parser for the TOU .SHP sprite body.
//
// Body layout — confirmed empirically across all 9 stock SHP files in
// `TOU/ships/`:
//
//   file_size = header_bytes + 32 * 3 * frame_w * frame_h
//   body_start = file_size - 32 * 3 * frame_w * frame_h
//
// `header_bytes` varies per ship (~592 + name_length); rather than
// computing it forward from the parsed header, we anchor the body
// from the END of the file using the frame dimensions decoded from
// the WH-anchor (`ShpHeader.hpp` already does that pass).
//
// Each frame holds `frame_w * frame_h` pixels of 3 bytes each
// (interleaved per-pixel triplet):
//
//   pixel(x, y) = body[ rotation * frame_size + (y * w + x) * 3 .. + 3 ]
//
// Channel meaning (visually verified on TIEF, FLYY, XWIN, BATM, DEST,
// PERH — see `scripts/decode_sprite.py` for the visual proof):
//   byte 0 — primary hull palette index (the ship silhouette)
//   byte 1 — secondary highlight / wing-edge palette index
//   byte 2 — cockpit / center detail palette index (small, central)
//
// Recommended composite for the cleanest sprite is `byte[2]` over
// `byte[0]` (byte 2 takes precedence when non-zero, else byte 0).
// Palette index 0 is transparent.
//
// The 0x18 = 24 byte at `anchor[4]` in the header is NOT the body's
// frame count (we verified the body has 32 frames in every stock
// ship). It's likely "core rotation steps" used for some gameplay
// computation — but irrelevant for sprite decoding.
//
// 32 rotations × 11.25° = 360°, sequence starts with "ship facing
// down" at frame 0 and proceeds counter-clockwise.

#include <cstddef>
#include <cstdint>
#include <span>

#include "ShpHeader.hpp"

namespace tou2d::shp {

/// Number of rotation frames per ship in the sprite sheet.
/// Confirmed in all 9 stock SHPs; the header's `rotationCount` byte
/// (0x18 = 24) is something else.
inline constexpr std::uint32_t kBodyRotationCount = 32;

/// Bytes per pixel in the body (interleaved triplet).
inline constexpr std::uint32_t kBodyBytesPerPixel = 3;

/// Body channel role indices.
enum class BodyChannel : std::uint8_t {
    Hull    = 0,  ///< Primary palette index — clean ship silhouette.
    Edge    = 1,  ///< Secondary highlight / wing-edge palette index.
    Cockpit = 2,  ///< Cockpit / center detail palette index.
};

/// Decoded sprite-body view — non-owning slices over the source file.
struct ParsedBody {
    std::size_t                   bodyStart = 0;     ///< Offset of the first frame byte.
    std::size_t                   frameSize = 0;     ///< `3 * frameWidth * frameHeight`.
    std::uint16_t                 frameWidth = 0;
    std::uint16_t                 frameHeight = 0;
    std::span<const std::uint8_t> body;              ///< Full body slice (32 frames).

    /// Returns the byte slice for `rotation` (0..31). Caller is
    /// responsible for ensuring `rotation < kBodyRotationCount`.
    [[nodiscard]] std::span<const std::uint8_t> frame(std::uint32_t rotation) const {
        return body.subspan(static_cast<std::size_t>(rotation) * frameSize, frameSize);
    }

    /// Returns the (byte0, byte1, byte2) triplet for pixel (x, y) in
    /// the given rotation. Out-of-range inputs are caller-validated.
    struct Pixel { std::uint8_t hull; std::uint8_t edge; std::uint8_t cockpit; };
    [[nodiscard]] Pixel pixel(std::uint32_t rotation,
                              std::uint32_t x, std::uint32_t y) const {
        const auto fb = frame(rotation);
        const std::size_t o = (static_cast<std::size_t>(y) * frameWidth + x) * kBodyBytesPerPixel;
        return { fb[o], fb[o + 1], fb[o + 2] };
    }
};

/// Anchor the body from the end of the file using the frame
/// dimensions decoded by `parseHeader`. Returns `false` if the file
/// is too small to contain a full 32-frame body.
inline bool parseBody(std::span<const std::uint8_t> data,
                      const ParsedHeader& header,
                      ParsedBody& out) {
    if (header.frameWidth == 0 || header.frameHeight == 0) return false;
    const std::size_t frameSize =
        static_cast<std::size_t>(header.frameWidth) *
        static_cast<std::size_t>(header.frameHeight) *
        kBodyBytesPerPixel;
    const std::size_t bodySize = static_cast<std::size_t>(kBodyRotationCount) * frameSize;
    if (data.size() < bodySize) return false;
    const std::size_t bodyStart = data.size() - bodySize;
    if (bodyStart < header.payloadStart) return false;

    out.bodyStart   = bodyStart;
    out.frameSize   = frameSize;
    out.frameWidth  = header.frameWidth;
    out.frameHeight = header.frameHeight;
    out.body        = data.subspan(bodyStart, bodySize);
    return true;
}

/// Composite a single rotation frame into a flat RGBA8 pixel buffer
/// (row-major, top-down). Caller provides `palette` as 256 RGB
/// triplets (already expanded to 8-bit per channel; see
/// `palette_from_vga6` in the importer CLI). `dst` must have
/// capacity `frameWidth * frameHeight * 4`. Palette index 0 is
/// rendered as (0, 0, 0, 0) — transparent.
///
/// Render rule: `byte[2]` (cockpit) takes precedence when non-zero,
/// else `byte[0]` (hull). `byte[1]` is currently unused — it carries
/// an edge-highlight channel that future renderers may consume.
inline void compositeRotation(const ParsedBody& body,
                              std::uint32_t rotation,
                              std::span<const std::uint8_t> palette,  // 256 * 3 = 768
                              std::span<std::uint8_t> dst) {
    const auto fb = body.frame(rotation);
    const std::size_t pixelCount =
        static_cast<std::size_t>(body.frameWidth) * body.frameHeight;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::uint8_t hull    = fb[i * 3 + 0];
        const std::uint8_t cockpit = fb[i * 3 + 2];
        const std::uint8_t idx     = cockpit ? cockpit : hull;
        if (idx == 0) {
            dst[i * 4 + 0] = 0;
            dst[i * 4 + 1] = 0;
            dst[i * 4 + 2] = 0;
            dst[i * 4 + 3] = 0;
        } else {
            dst[i * 4 + 0] = palette[idx * 3 + 0];
            dst[i * 4 + 1] = palette[idx * 3 + 1];
            dst[i * 4 + 2] = palette[idx * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
    }
}

} // namespace tou2d::shp
