// LevConfig — decoded form of the original-TOU `.lev` "/KEY value" blob.
//
// The .lev container stores its per-level game-design parameters
// (water color, gravity, ambient sound, etc.) in a fixed-offset binary
// block starting at file offset 0x122. The byte-level layout was
// reversed against the 4 shipped originals on 2026-05-31 by cross-
// referencing each level's bytes with its makelev `<Stem>.txt` source.
//
// Header-only and dependency-free so tests can pin the format without
// linking the importer CLI.
//
// See examples/tou2d/TOU_RE.md "section3 — partial decode" for the
// full offset-by-offset table and the cross-level validation matrix.

#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace tou2d {

/// File offset where the /KEY value block begins inside a .lev.
inline constexpr std::size_t kLevConfigStart = 0x122;

/// File offset where the /GGTHEME string begins (16 bytes NUL-padded).
inline constexpr std::size_t kLevConfigThemeStart = 0x130;

/// File offset where the trailing /GGSHAPE..randomseed block begins.
inline constexpr std::size_t kLevConfigTailStart = 0x1B0;

/// Minimum .lev size that still has a complete /KEY blob.
inline constexpr std::size_t kLevConfigMinFileBytes = 0x1B8;

/// Decoded /KEY value block. Defaults match makelev/Normal.txt's
/// shipped defaults so a partial parse (or zero-initialised block)
/// still yields sane gameplay knobs.
struct LevConfig {
    bool          para         = false;   ///< 0x122 /PARA bool
    std::uint8_t  civil        = 10;      ///< 0x123 /CIVIL  0..100
    std::uint8_t  bomb         = 5;       ///< 0x124 /BOMB   0..100
    std::uint8_t  watercR      = 0;       ///< 0x125 /WATERC R
    std::uint8_t  watercG      = 120;     ///< 0x126 /WATERC G
    std::uint8_t  watercB      = 255;     ///< 0x127 /WATERC B
    bool          disableRun   = false;   ///< 0x128 /DISABLERUN
    std::uint8_t  gravity      = 10;      ///< 0x129 /GRAVITY    ×10
    std::uint8_t  resistance   = 10;      ///< 0x12A /RESISTANCE ×10
    std::uint8_t  collDamage   = 10;      ///< 0x12B /COLLDAMAGE ×10
    std::uint8_t  bouncing     = 10;      ///< 0x12C /BOUNCING   ×10
    std::uint8_t  ambient      = 0;       ///< 0x12D /AMBIENT
    std::uint8_t  parallaxAt   = 2;       ///< 0x12E /PARALLAXAT
    bool          ggLevel      = false;   ///< 0x12F /GGLEVEL bool
    std::string   ggTheme;                ///< 0x130..0x140 16-byte string
    bool          ggShape      = true;    ///< 0x1B0 /GGSHAPE
    std::uint8_t  repair       = 20;      ///< 0x1B1 /REPAIR
    std::uint8_t  stuffD       = 20;      ///< 0x1B2 /STUFFD
    std::uint8_t  signD        = 20;      ///< 0x1B3 /SIGND
    std::uint16_t randomSeed   = 0;       ///< 0x1B6..0x1B8 u16 LE
};

/// Parse the /KEY value blob out of a raw .lev file buffer.
///
/// `data` must be the full .lev bytes (the offsets here are absolute
/// file offsets). Returns true on success; false if the buffer is
/// too short to cover the entire /KEY block.
inline bool parseLevConfig(std::span<const std::uint8_t> data,
                           LevConfig& cfg) {
    if (data.size() < kLevConfigMinFileBytes) return false;
    const auto u8 = [&](std::size_t off) -> std::uint8_t { return data[off]; };
    cfg.para        = u8(0x122) != 0;
    cfg.civil       = u8(0x123);
    cfg.bomb        = u8(0x124);
    cfg.watercR     = u8(0x125);
    cfg.watercG     = u8(0x126);
    cfg.watercB     = u8(0x127);
    cfg.disableRun  = u8(0x128) != 0;
    cfg.gravity     = u8(0x129);
    cfg.resistance  = u8(0x12A);
    cfg.collDamage  = u8(0x12B);
    cfg.bouncing    = u8(0x12C);
    cfg.ambient     = u8(0x12D);
    cfg.parallaxAt  = u8(0x12E);
    cfg.ggLevel     = u8(0x12F) != 0;
    {
        const char* t = reinterpret_cast<const char*>(data.data() + 0x130);
        std::size_t tlen = 0;
        while (tlen < 16 && t[tlen] != '\0') ++tlen;
        cfg.ggTheme.assign(t, tlen);
    }
    cfg.ggShape     = u8(0x1B0) != 0;
    cfg.repair      = u8(0x1B1);
    cfg.stuffD      = u8(0x1B2);
    cfg.signD       = u8(0x1B3);
    cfg.randomSeed  = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(u8(0x1B6)) |
       (static_cast<std::uint16_t>(u8(0x1B7)) << 8));
    return true;
}

} // namespace tou2d
