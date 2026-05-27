#pragma once

// Header-only loader for the 768-byte TOU palette files (`Pal.col`,
// `SHIPAL.COL`).
//
// Format: 256 entries, each 3 bytes (R, G, B) in VGA convention — i.e.
// each channel is a 6-bit value (0..63) stored in a byte. The original
// VGA DAC scales these to 0..255 for display. Quick check on Pal.col
// bytes shows the maximum channel value across the file is 0x3F (=63),
// consistent with the 6-bit convention; we expand by (b << 2) | (b >> 4)
// — the standard bit-replication that maps 0..63 to 0..255 evenly
// (so 0x3F → 0xFF exactly, 0x00 → 0x00 exactly).
//
// Lives in `examples/tou2d/` so the importer CLI and tests can both
// include it without dragging in the engine. Pure header.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tou2d::shp {

inline constexpr std::size_t kPaletteEntries = 256;
inline constexpr std::size_t kPaletteFileBytes = kPaletteEntries * 3;

struct PaletteEntry {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

struct Palette {
    std::array<PaletteEntry, kPaletteEntries> entries{};
};

inline std::uint8_t expand6to8(std::uint8_t v6) noexcept {
    // The high 6 bits are the real value; the low 2 bits replicate the
    // top 2 of the value so 0x3F maps cleanly to 0xFF.
    const std::uint8_t clamped = v6 & 0x3F;
    return static_cast<std::uint8_t>((clamped << 2) | (clamped >> 4));
}

/// Parses a 768-byte VGA palette. Returns false on wrong size.
inline bool parsePalette(std::span<const std::uint8_t> raw, Palette& out) {
    if (raw.size() != kPaletteFileBytes) return false;
    for (std::size_t i = 0; i < kPaletteEntries; ++i) {
        const std::size_t off = i * 3;
        out.entries[i].r = expand6to8(raw[off + 0]);
        out.entries[i].g = expand6to8(raw[off + 1]);
        out.entries[i].b = expand6to8(raw[off + 2]);
    }
    return true;
}

} // namespace tou2d::shp
