#pragma once

// Header-only parser for the TOU .SHP file header. Lives in
// `examples/tou2d/` so both the importer CLI and the matching test
// (tests/tou2d_shp_import_test.cpp) can include the same source of
// truth without dragging in the rest of the engine.
//
// Parser scope (what we DO decode — empirically confirmed across all
// 9 stock SHP files in `TOU/ships/`):
//
//   1. Leading 0x00 padding / version byte.
//   2. NUL-terminated ASCII display name at offset 1.
//   3. 3-byte stat triplet (Strength / Thrusters / Turning encodings).
//   4. 4-byte stat extra block. Byte 3 (`statExtra[3]`) is the
//      ship's max-HP at the engine's internal scale (manual quotes
//      Strength on a 1-10 scale; observed max-HP correlates with the
//      "weak ship" entries):
//        * 0x32 = 50: BATM, BEE2, DEST, FLYY, PERU, TIEF, XWIN  (7/9)
//        * 0x28 = 40: SPED  (manual Strength = 1.5 — "Micro Speedie")
//        * 0x14 = 20: PERH  ("Butterfly" — not in the manual table;
//                            evidently a deliberately fragile variant)
//      The first three bytes vary per ship and don't correlate with
//      anything in the manual — likely engine-internal physics
//      coefficients (mass, drag, turn-acceleration?).
//   5. A variable-length section ending in the anchor `WW 00 HH 00
//      18 20` (frame width LE-u16, frame height LE-u16, byte 0x18 = 24
//      rotation steps, trailing constant 0x20). The bytes between the
//      stat-extra block and this anchor differ per ship (a presumed
//      "section marker" begins `?? 01 02 01 05 04` in 8 of 9 ships;
//      XWIN's variant is `01 01 02 01 02 03`). Parser searches for
//      the W/H/rot anchor within `kAnchorSearchSpan` bytes.
//
// Sprite body decoding LANDED — see `ShpBody.hpp` for the parser
// and `scripts/decode_sprite.py` for the visual verification. Short
// version of the body layout:
//
//   file_size = header_bytes + 32 * 3 * frameWidth * frameHeight
//   body_start = file_size - 32 * 3 * frameWidth * frameHeight
//
// Each rotation frame holds `frameWidth * frameHeight` pixels at 3
// bytes per pixel (interleaved triplet): byte 0 = hull palette index,
// byte 1 = edge highlight, byte 2 = cockpit/detail. Composite is
// "byte 2 over byte 0" (cockpit overlays hull). Index 0 transparent.
// 32 rotations × 11.25° starting from "ship facing down" CCW.
//
// The 0x18 = 24 at `anchor[4]` here is therefore NOT the body's
// rotation count (the body is always 32 frames). It's a different
// gameplay constant we don't currently consume.
//
// What's STILL deferred:
//   * The exact role of the per-ship marker region (8 ships:
//     `?? 01 02 01 05 04`; XWIN: `01 01 02 01 02 03`).
//   * The 500+ bytes between the W/H anchor and the body start —
//     candidate fields include per-rotation offset tables (never
//     needed for decode now that the body is fixed-stride),
//     animation timing, or weapon hardpoints. Not decoded.
//
// See TOU_PLAN.md § M4.7c for the body breakthrough writeup and
// § M4.8 for the runtime renderer hookup recipe.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace tou2d::shp {

/// Defensive cap on display-name length — the manual's longest stock
/// ship name ("Imperium Tie Fighter") is 20 chars. 64 leaves plenty of
/// headroom for fan-made ships while still bounding work for malformed
/// input.
inline constexpr std::size_t kNameMax = 64;

/// How far past the stat-extra block to scan for the W/H/rot anchor.
/// All 9 stock ships have the anchor within ≤ 22 bytes of the
/// stat-extra block; 96 gives generous headroom without making
/// malformed input expensive.
inline constexpr std::size_t kAnchorSearchSpan = 96;

/// Decoded header fields. `payloadStart` is the offset of the first
/// byte past the W/H/rot anchor — that's the start of the opaque
/// sprite section the parser does NOT yet decode.
struct ParsedHeader {
    std::string                 displayName;
    std::array<std::uint8_t, 3> statTriplet{};   // bytes after name terminator
    std::array<std::uint8_t, 4> statExtra{};     // 4 bytes after stat triplet
    std::uint8_t                maxHp = 0;       // == statExtra[3]; 0x32 in all stock files
    std::uint16_t               frameWidth = 0;  // from "WW 00 HH 00 18 20" anchor
    std::uint16_t               frameHeight = 0; // same anchor
    std::uint8_t                rotationCount = 0; // anchor[4]; 0x18 = 24 in all stock files
    std::size_t                 anchorOffset = 0;  // byte offset of the W/H/rot anchor
    std::size_t                 payloadStart = 0;  // anchorOffset + 6 (past the 6-byte anchor)
};

/// Pure parser — no I/O. Returns true on success; on failure the
/// out-param is left in an unspecified state. See file-header comment
/// for the validation rules.
inline bool parseHeader(std::span<const std::uint8_t> data,
                        ParsedHeader& out) {
    if (data.size() < 12)        return false;
    if (data[0] != 0x00)         return false;

    std::size_t nameEnd = 1;
    while (nameEnd < data.size() &&
           nameEnd < 1 + kNameMax &&
           data[nameEnd] != 0x00) {
        const std::uint8_t b = data[nameEnd];
        if (b < 0x20 || b > 0x7E) return false;
        ++nameEnd;
    }
    if (nameEnd >= 1 + kNameMax)        return false;
    if (nameEnd >= data.size())         return false;
    if (data[nameEnd] != 0x00)          return false;
    if (nameEnd == 1)                   return false;  // empty name
    // Need 3 stat + 4 extra bytes after the name terminator.
    if (nameEnd + 7 >= data.size())     return false;

    out.displayName.assign(reinterpret_cast<const char*>(data.data() + 1),
                           nameEnd - 1);
    out.statTriplet[0] = data[nameEnd + 1];
    out.statTriplet[1] = data[nameEnd + 2];
    out.statTriplet[2] = data[nameEnd + 3];
    out.statExtra[0]   = data[nameEnd + 4];
    out.statExtra[1]   = data[nameEnd + 5];
    out.statExtra[2]   = data[nameEnd + 6];
    out.statExtra[3]   = data[nameEnd + 7];
    out.maxHp          = out.statExtra[3];

    // Search for the W/H/rot anchor: "WW 00 HH 00 18 20", where WW
    // and HH are non-zero width/height LE-u16, 0x18 is the (stock)
    // rotation-count constant, and 0x20 is a trailing invariant.
    // Anchor offset varies per ship (depends on the per-ship marker
    // region's length); a bounded linear scan is cheap.
    const std::size_t searchStart = nameEnd + 8;
    if (searchStart + 6 > data.size()) return false;
    const std::size_t searchEnd =
        (searchStart + kAnchorSearchSpan + 6 <= data.size())
            ? searchStart + kAnchorSearchSpan
            : data.size() - 6;

    std::size_t anchor = static_cast<std::size_t>(-1);
    for (std::size_t i = searchStart; i < searchEnd; ++i) {
        if (data[i + 1] != 0x00) continue;
        if (data[i + 3] != 0x00) continue;
        if (data[i + 4] != 0x18) continue;
        if (data[i + 5] != 0x20) continue;
        if (data[i]     == 0x00) continue;  // width  must be non-zero
        if (data[i + 2] == 0x00) continue;  // height must be non-zero
        anchor = i;
        break;
    }
    if (anchor == static_cast<std::size_t>(-1)) return false;

    out.frameWidth =
        static_cast<std::uint16_t>(data[anchor]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[anchor + 1]) << 8);
    out.frameHeight =
        static_cast<std::uint16_t>(data[anchor + 2]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[anchor + 3]) << 8);
    out.rotationCount = data[anchor + 4];
    out.anchorOffset  = anchor;
    out.payloadStart  = anchor + 6;
    return true;
}

} // namespace tou2d::shp
