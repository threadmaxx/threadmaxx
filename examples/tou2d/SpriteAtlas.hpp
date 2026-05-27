#pragma once

// M4.8 — per-ship sprite atlas. Header-only loader that reads a TOU
// `.SHP` file, decodes its header + 32-frame body, and stages an RGBA8
// buffer per rotation using a caller-supplied `ShipColors` so each
// player slot can be tinted (per-team faction color).
//
// Layout in memory:
//   * `frameWidth` × `frameHeight` RGBA pixels per rotation.
//   * 32 rotations laid out as 32 independent buffers (NOT a contiguous
//     atlas) — the compositor blits one frame per ship per tick and
//     doesn't need them packed.
//
// Lifetime: read-only after construction. SpriteCompositor borrows
// const refs.

#include "ShpBody.hpp"
#include "ShpHeader.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace tou2d {

struct SpriteAtlas {
    std::uint16_t frameWidth  = 0;
    std::uint16_t frameHeight = 0;
    /// 32 entries. Each entry is `frameWidth * frameHeight * 4` bytes.
    std::array<std::vector<std::uint8_t>, shp::kBodyRotationCount> frames;

    [[nodiscard]] bool valid() const noexcept {
        return frameWidth != 0 && frameHeight != 0;
    }
};

/// Read `path` from disk, decode header + body, composite 32 rotations
/// at `colors`, write into `out`. Returns true on success. Failure
/// leaves `out` in an unspecified state but the caller can still fall
/// back to the cube-render path.
inline bool loadSpriteAtlas(const std::filesystem::path& path,
                            const shp::ShipColors& colors,
                            SpriteAtlas& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize bytes = f.tellg();
    if (bytes <= 0) return false;
    f.seekg(0);
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(bytes));
    f.read(reinterpret_cast<char*>(raw.data()), bytes);
    if (!f) return false;

    shp::ParsedHeader header;
    if (!shp::parseHeader(std::span<const std::uint8_t>(raw.data(), raw.size()),
                          header)) {
        std::fprintf(stderr, "[SpriteAtlas] parseHeader failed for %s\n",
                     path.string().c_str());
        return false;
    }

    shp::ParsedBody body;
    if (!shp::parseBody(std::span<const std::uint8_t>(raw.data(), raw.size()),
                        header, body)) {
        std::fprintf(stderr, "[SpriteAtlas] parseBody failed for %s\n",
                     path.string().c_str());
        return false;
    }

    out.frameWidth  = header.frameWidth;
    out.frameHeight = header.frameHeight;
    const std::size_t rgbaSize =
        static_cast<std::size_t>(header.frameWidth) *
        static_cast<std::size_t>(header.frameHeight) * 4u;
    for (std::uint32_t r = 0; r < shp::kBodyRotationCount; ++r) {
        out.frames[r].assign(rgbaSize, std::uint8_t{0});
        shp::compositeRotationCentered(
            body, r, colors,
            std::span<std::uint8_t>(out.frames[r].data(), rgbaSize));
    }
    return true;
}

/// M4.8 — per-team color palettes. Slot 0 = warm yellow, slot 1 = blue,
/// slot 2 = red, slot 3 = green. Hull stays neutral grey across slots
/// (it's the ship's structural color in the original game); cockpit
/// stays warm amber. Only the TEAM channel changes per slot — that's
/// the faction-identifying color the original TOU used.
inline constexpr std::array<shp::ShipColors, 4> kSlotColors = {{
    shp::ShipColors{200, 200, 200,   240, 200, 50,   255, 180, 80},  // P1 yellow
    shp::ShipColors{200, 200, 200,    50, 130, 240,  255, 180, 80},  // P2 blue
    shp::ShipColors{200, 200, 200,   230,  60, 60,   255, 180, 80},  // P3 red
    shp::ShipColors{200, 200, 200,    60, 200, 90,   255, 180, 80},  // P4 green
}};

} // namespace tou2d
