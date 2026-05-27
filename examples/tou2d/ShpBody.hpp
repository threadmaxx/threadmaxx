#pragma once

// Header-only parser + renderer for the TOU .SHP sprite body.
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
// === Channel model (M4.7d breakthrough) ===
//
// The three bytes per pixel are NOT three palette indices. They are
// three INDEPENDENT intensity channels (0..255), one per shaded region:
//
//   byte 0 — hull    intensity (cross-brackets, structural elements)
//   byte 1 — team    intensity (wings / solar-panels / faction-color region)
//   byte 2 — cockpit intensity (cockpit-sphere / center detail)
//
// Each pixel is rendered as the additive blend of the three colored
// contributions (per-channel clamped to 255), with alpha = 1 when any
// of the three bytes is non-zero. See `ShipColors` / `compositeRotationCentered`.
//
// Verified by dumping TIEF frame 31: b0 holds small 2-7 values along
// the T-bar plus brighter highlight values; b1 has a smooth 0-255
// gradient in two symmetric clusters (left+right wings); b2 has a
// smooth 0-255 gradient in the central cockpit blob. See M4.7d in
// TOU_PLAN.md for the full visual investigation.
//
// === Frame geometry (M4.7d breakthrough) ===
//
// The body bytes are stored toroidally — the visible ship wraps around
// the (0, 0) origin of the frame. The wrap origin is encoded as a
// sentinel pixel located at a deterministic flat position per rotation:
//
//   flat_position(N) = W * H - 6 * (31 - N)
//
// The +6 step is exactly the size of a 3-pixel metadata trailer that
// every frame carries at that position:
//
//   trailer[0] (pixel +0) = (b0=0,  b1=0,   b2=2)   — magic marker
//   trailer[1] (pixel +2) = (b0=0,  b1=24,  b2=0)   — rotation count (0x18)
//   trailer[2] (pixel +4) = (b0=W,  b1=0,   b2=W)   — frame width
//
// For frame 31 the formula evaluates to W*H (one past the last pixel),
// so frame 31 has no sentinel and is already centered — it must be
// rendered as-is (no shift, no trailer suppression).
//
// === Centering recipe (M4.7d) ===
//
// For rotations 0..30:
//   1. Decode sentinel (sx, sy) from the formula above.
//   2. Apply primary toroidal shift `(sx, sy + 1)`:
//        post(x, y) = pre((x + sx) % W, (y + sy + 1) % H)
//      Using `sy + 1` (instead of `sy`) puts the sentinel row at the
//      BOTTOM of the post-shift frame (row H-1) rather than at row 0.
//      Note: this leaves the sentinel row's height-1 strip visually
//      "above the ship" in some rotations and "below" in others — left
//      as a TBD; visual inspection at engine-side scale will tell us
//      whether to keep, flip, or hide that strip.
//   3. Apply secondary horizontal shift on the TOP half (post-shift
//      rows 0..H-sy-2 = pre-shift rows below the sentinel) by +6
//      columns. The "top half" goes empty when sy == H-1 (sentinel on
//      the bottom row pre-shift) and the secondary shift is a no-op.
//   4. Render the 3 trailer pixels at post-shift (0, H-1), (2, H-1),
//      (4, H-1) as fully transparent.
//
// For rotation 31: render straight through, no shift, no trailer skip.
//
// === Color model (M4.7d) ===
//
// Blend each of the three channels' intensities into the final pixel
// using configurable hull / team / cockpit colors:
//
//   r = clamp255( hullR  * b0 / 255
//               + teamR  * b1 / 255
//               + cockptR * b2 / 255 )
//   ... and similarly for g, b.
//   a = (b0 || b1 || b2) ? 255 : 0
//
// The legacy palette-based `compositeRotation` (byte[2]-over-byte[0])
// is kept for tools that want to inspect the raw channels in VGA-palette
// form, but the in-engine renderer should use `compositeRotationCentered`.
//
// 32 rotations × 11.25° = 360°, sequence starts with "ship facing
// down" at frame 0 and proceeds counter-clockwise.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "ShpHeader.hpp"

namespace tou2d::shp {

/// Number of rotation frames per ship in the sprite sheet.
/// Confirmed in all 9 stock SHPs; the header's `rotationCount` byte
/// (0x18 = 24) is something else.
inline constexpr std::uint32_t kBodyRotationCount = 32;

/// Bytes per pixel in the body (interleaved triplet).
inline constexpr std::uint32_t kBodyBytesPerPixel = 3;

/// Stride (in pixels) between consecutive frames' sentinel positions.
/// `flat_pos(N) = W*H - kBodySentinelStride * (31 - N)`.
inline constexpr std::uint32_t kBodySentinelStride = 6;

/// Constant secondary horizontal shift applied to the post-shift top
/// half (rows above the sentinel pre-shift). Empirically `+6` columns
/// best matches the original-game centered sprite — see M4.7d.
inline constexpr std::uint32_t kBodyTopHalfShift = 6;

/// Trailer is 3 pixels at flat offsets +0, +2, +4 from the sentinel.
/// They encode per-frame metadata (a magic marker, the rotation count,
/// and the frame width). After the centering shift they land at
/// post-shift (0, H-1), (2, H-1), (4, H-1) — they MUST be cleared to
/// transparent at render time.
inline constexpr std::uint32_t kBodyTrailerCols[3] = {0, 2, 4};

/// Body channel role indices.
enum class BodyChannel : std::uint8_t {
    Hull    = 0,  ///< Hull / structural-bracket intensity (0..255).
    Team    = 1,  ///< Team / wing / faction-color intensity (0..255).
    Cockpit = 2,  ///< Cockpit / center-detail intensity (0..255).
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
    /// Field names retained for back-compat with the M4.7c tests; the
    /// updated channel meaning is hull / team / cockpit (see file
    /// header).
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

/// Pixel coordinates of the wrap-origin sentinel for a frame, or
/// `std::nullopt` for frame 31 (which has no sentinel and is already
/// centered).
struct Sentinel { std::uint32_t x; std::uint32_t y; };
[[nodiscard]] inline std::optional<Sentinel>
primarySentinel(std::uint16_t frameWidth,
                std::uint16_t frameHeight,
                std::uint32_t rotation) noexcept {
    const std::uint32_t area =
        static_cast<std::uint32_t>(frameWidth) * frameHeight;
    if (rotation >= kBodyRotationCount) return std::nullopt;
    const std::uint32_t kMax = (kBodyRotationCount - 1) * kBodySentinelStride;
    if (rotation == kBodyRotationCount - 1) return std::nullopt;   // frame 31: no sentinel
    const std::uint32_t back = kMax - rotation * kBodySentinelStride;
    if (back > area) return std::nullopt;                          // malformed
    const std::uint32_t flat = area - back;
    return Sentinel{ flat % frameWidth, flat / frameWidth };
}

/// Renderer colors for the three intensity channels. Defaults are
/// the in-game-ish hull / team / cockpit triple verified by visual
/// inspection at M4.7d.
struct ShipColors {
    std::uint8_t hullR    = 180; std::uint8_t hullG    = 180; std::uint8_t hullB    = 180;
    std::uint8_t teamR    =  40; std::uint8_t teamG    = 100; std::uint8_t teamB    = 220;
    std::uint8_t cockpitR = 255; std::uint8_t cockpitG = 180; std::uint8_t cockpitB =  80;
};

/// Composite a single rotation frame into a flat RGBA8 pixel buffer
/// (row-major, top-down) using the legacy palette-index-as-channel
/// interpretation (byte[2] over byte[0] priority; byte[1] unused).
/// Retained for tools that want to inspect raw channel data via the
/// VGA palette — not the renderer the game should consume.
///
/// `palette` is 256 RGB triplets (already expanded to 8-bit per channel;
/// see `palette_from_vga6` in the importer CLI). `dst` must have
/// capacity `frameWidth * frameHeight * 4`. Palette index 0 is rendered
/// as (0, 0, 0, 0) transparent.
inline void compositeRotation(const ParsedBody& body,
                              std::uint32_t rotation,
                              std::span<const std::uint8_t> palette,
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

namespace detail {
inline std::uint8_t blendChannel(std::uint8_t color, std::uint8_t intensity) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(color) * intensity + 127u) / 255u);
}
inline std::uint8_t addSat(std::uint32_t a, std::uint32_t b) noexcept {
    const std::uint32_t s = a + b;
    return static_cast<std::uint8_t>(s > 255u ? 255u : s);
}
} // namespace detail

/// Composite a single rotation frame into a flat RGBA8 pixel buffer
/// (row-major, top-down) using the M4.7d centering recipe + blend
/// color model. This is the renderer the game should consume.
///
/// Recipe summary (see file header for the full discussion):
///   * Rotations 0..30: toroidal shift by `(sx, sy + 1)`, then top-half
///     `+kBodyTopHalfShift` column shift, then suppress the 3 trailer
///     pixels at post-shift `(0, H-1)`, `(2, H-1)`, `(4, H-1)`.
///   * Rotation 31: no shift, no trailer suppression (already centered).
///
/// Color: additive blend of `colors.hull * b0/255`, `colors.team * b1/255`,
/// `colors.cockpit * b2/255` clamped to 255 per channel. Alpha = 255
/// when any of (b0, b1, b2) is non-zero, else 0.
///
/// `dst` must have capacity `frameWidth * frameHeight * 4`.
inline void compositeRotationCentered(const ParsedBody& body,
                                      std::uint32_t rotation,
                                      const ShipColors& colors,
                                      std::span<std::uint8_t> dst) {
    const std::uint16_t W = body.frameWidth;
    const std::uint16_t H = body.frameHeight;
    const auto fb = body.frame(rotation);
    const auto sentinelOpt = primarySentinel(W, H, rotation);

    // Single-pass: compute the source (x, y) for every destination pixel,
    // including the secondary top-half +6 shift, and write the blended RGBA.
    const bool   suppressTrailer = sentinelOpt.has_value();
    const Sentinel sentinel = sentinelOpt.value_or(Sentinel{0, 0});
    const std::uint32_t sx     = sentinel.x;
    const std::uint32_t effSy  = suppressTrailer ? (sentinel.y + 1u) % H : 0u;
    const std::uint32_t topHeight =
        suppressTrailer
            ? static_cast<std::uint32_t>(H - sentinel.y - 1u)  // >= 0 and < H
            : 0u;
    const std::uint32_t trailerRow = suppressTrailer ? (H - 1u) : 0u;

    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            // Apply secondary top-half shift first (it affects which COLUMN
            // of the post-primary-shift buffer feeds this destination).
            std::uint32_t postX = x;
            if (suppressTrailer && topHeight > 0 && y < topHeight) {
                postX = (x + kBodyTopHalfShift) % W;
            }
            // Trailer suppression — check in post-primary-shift coordinates
            // (the same row/col the trailer would naturally land on).
            const bool isTrailer = suppressTrailer && y == trailerRow &&
                (postX == kBodyTrailerCols[0] ||
                 postX == kBodyTrailerCols[1] ||
                 postX == kBodyTrailerCols[2]);
            const std::uint32_t srcX = (postX + sx) % W;
            const std::uint32_t srcY = (y + effSy) % H;
            const std::size_t o =
                (static_cast<std::size_t>(srcY) * W + srcX) * kBodyBytesPerPixel;
            const std::uint8_t b0 = fb[o + 0];
            const std::uint8_t b1 = fb[o + 1];
            const std::uint8_t b2 = fb[o + 2];

            const std::size_t di =
                (static_cast<std::size_t>(y) * W + x) * 4u;
            if (isTrailer || (b0 == 0 && b1 == 0 && b2 == 0)) {
                dst[di + 0] = 0;
                dst[di + 1] = 0;
                dst[di + 2] = 0;
                dst[di + 3] = 0;
                continue;
            }
            const std::uint32_t rh = detail::blendChannel(colors.hullR,    b0);
            const std::uint32_t gh = detail::blendChannel(colors.hullG,    b0);
            const std::uint32_t bh = detail::blendChannel(colors.hullB,    b0);
            const std::uint32_t rt = detail::blendChannel(colors.teamR,    b1);
            const std::uint32_t gt = detail::blendChannel(colors.teamG,    b1);
            const std::uint32_t bt = detail::blendChannel(colors.teamB,    b1);
            const std::uint32_t rc = detail::blendChannel(colors.cockpitR, b2);
            const std::uint32_t gc = detail::blendChannel(colors.cockpitG, b2);
            const std::uint32_t bc = detail::blendChannel(colors.cockpitB, b2);
            dst[di + 0] = detail::addSat(rh, detail::addSat(rt, rc));
            dst[di + 1] = detail::addSat(gh, detail::addSat(gt, gc));
            dst[di + 2] = detail::addSat(bh, detail::addSat(bt, bc));
            dst[di + 3] = 255;
        }
    }
}

} // namespace tou2d::shp
