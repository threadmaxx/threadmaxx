#pragma once

// tou2d M5.5 — procedural terrain generator.
//
// Deterministic fill of a `TerrainGrid` from a small config POD. Same
// (seed, ggLevel, stuffDensity, perimeterBedrock) → byte-identical
// `attr[]` and `hp[]`. The generator does not require theme assets —
// it works at the attribute level only (Air vs Solid). When theme
// JPGs land later they paint the visual layer on top; the cell
// shape is what this header generates.
//
// Algorithm (kept simple and deterministic):
//   1. cellsX/cellsY come from `ggLevel` (see kGgLevelCells below).
//   2. `grid.reset(cellsX, cellsY)`.
//   3. Bottom 1/8 of the canvas filled solid (ground).
//   4. `nBlobs` ≈ stuffDensity * area / 5000, clamped to [1, 256].
//      For each blob, sample (center, rX, rY) from one mt19937; stamp
//      an elliptical solid region with `hp = kBlobHp`.
//   5. Optional 1-cell bedrock perimeter (`hp = 0xFF`).
//
// Header-only so unit tests can link against nothing beyond
// DemoTypes.hpp. Game-side wiring lives in TouGame.cpp.

#include "DemoTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <random>

namespace tou2d {

/// Maps `GGLEVEL` (0..4) to grid cell extent. Mirrors the original
/// TOU's size classes: tiny / small / medium / large / xlarge. Values
/// land on multiples of 16 so cell counts divide cleanly when the
/// generator splits the canvas into thirds for blob centers.
inline constexpr std::int32_t kGgLevelCells[5] = {
     48,   //  0 — tiny     (48x48)
     80,   //  1 — small    (80x80)
    112,   //  2 — medium   (112x112, default)
    160,   //  3 — large    (160x160)
    208,   //  4 — xlarge   (208x208)
};

/// HP assigned to procedurally-stamped solid cells. Matches the
/// LevelLoader default (≈ 3 dumbfire shots / 5 spread pellets).
inline constexpr std::uint8_t kProcBlobHp = 24;

/// Configuration for one generator invocation. Defaults produce a
/// playable medium-size level on seed 0.
///
/// M5.7 — the former `_pad` byte is now `repairTileCount`. Defaults to
/// 0 so existing tests (and the pre-M5.7 v2 replay path, which stored
/// a zero byte at the equivalent header offset) reproduce identically.
/// `main.cpp` bumps the default to 8 for fresh CLI runs.
struct ProceduralLevelConfig {
    std::uint32_t seed             = 0;   ///< RANDOMSEED equivalent
    std::uint8_t  ggLevel          = 2;   ///< 0..4 → kGgLevelCells
    std::uint8_t  stuffDensity     = 50;  ///< STUFFD equivalent, 0..100
    std::uint8_t  perimeterBedrock = 1;   ///< 1 ring of 0xFF if non-zero
    std::uint8_t  repairTileCount  = 0;   ///< M5.7 — sprinkled Air→Repair cells
};
static_assert(sizeof(ProceduralLevelConfig) == 8,
              "ProceduralLevelConfig must stay 8 bytes — replay header re-uses the same layout");

/// Result info — matches the LoadedLevelInfo shape so the caller can
/// log the same line whether it came from .lev or the generator.
struct ProceduralLevelInfo {
    bool          loaded     = false;
    std::int32_t  cellsX     = 0;
    std::int32_t  cellsY     = 0;
    std::int32_t  solidCount = 0;
    std::uint32_t seedUsed   = 0;
};

namespace detail {

/// Clamp ggLevel into the valid range without UB on a stray byte.
inline std::uint8_t clampGgLevel(std::uint8_t v) noexcept {
    return v > 4 ? std::uint8_t{4} : v;
}

/// Clamp stuffDensity to [0, 100].
inline std::uint8_t clampDensity(std::uint8_t v) noexcept {
    return v > 100 ? std::uint8_t{100} : v;
}

/// Stamp a filled axis-aligned ellipse centered at (worldCx, worldCy).
/// `hp` and `attr` are written via `grid.setSolid`. Out-of-bounds cells
/// are skipped via `grid.inBounds`. World cell convention follows the
/// TerrainGrid contract (centered on origin).
inline void stampEllipse(TerrainGrid& grid,
                         std::int32_t worldCx, std::int32_t worldCy,
                         std::int32_t rX, std::int32_t rY,
                         std::uint8_t hp,
                         std::int32_t& solidCount) noexcept {
    if (rX <= 0 || rY <= 0) return;
    const std::int64_t rX2 = static_cast<std::int64_t>(rX) * rX;
    const std::int64_t rY2 = static_cast<std::int64_t>(rY) * rY;
    for (std::int32_t dy = -rY; dy <= rY; ++dy) {
        for (std::int32_t dx = -rX; dx <= rX; ++dx) {
            const std::int64_t lhs =
                static_cast<std::int64_t>(dx) * dx * rY2 +
                static_cast<std::int64_t>(dy) * dy * rX2;
            if (lhs > rX2 * rY2) continue;
            const std::int32_t cx = worldCx + dx;
            const std::int32_t cy = worldCy + dy;
            if (!grid.inBounds(cx, cy)) continue;
            // Skip cells already solid — the count stays accurate even
            // when blobs overlap.
            if (grid.attrAt(cx, cy) != Attribute::Air) continue;
            grid.setSolid(cx, cy, hp, Attribute::Solid);
            ++solidCount;
        }
    }
}

} // namespace detail

/// Generate a level into `grid`. Same (cfg.seed, cfg.ggLevel,
/// cfg.stuffDensity, cfg.perimeterBedrock) always produces the same
/// post-call grid state. The single `std::mt19937` is seeded only
/// from `cfg.seed` so no external state can leak in.
inline ProceduralLevelInfo generateProceduralLevel(
        TerrainGrid& grid,
        const ProceduralLevelConfig& cfg) noexcept {
    ProceduralLevelInfo info;
    info.seedUsed = cfg.seed;

    const std::uint8_t  lvl     = detail::clampGgLevel(cfg.ggLevel);
    const std::uint8_t  density = detail::clampDensity(cfg.stuffDensity);
    const std::int32_t  cells   = kGgLevelCells[lvl];
    info.cellsX = cells;
    info.cellsY = cells;

    grid.reset(cells, cells);

    const std::int32_t halfX = cells / 2;
    const std::int32_t halfY = cells / 2;

    // Ground floor — bottom 1/8 of the canvas solid. World Y convention
    // has +halfY at top, -(cells - halfY - 1) at bottom; floor lives in
    // the negative-Y rows.
    const std::int32_t floorRows = std::max(2, cells / 8);
    const std::int32_t floorYMin = -(cells - halfY - 1);
    const std::int32_t floorYMax = floorYMin + floorRows - 1;
    for (std::int32_t cy = floorYMin; cy <= floorYMax; ++cy) {
        for (std::int32_t cx = -halfX; cx <= cells - halfX - 1; ++cx) {
            grid.setSolid(cx, cy, kProcBlobHp, Attribute::Solid);
            ++info.solidCount;
        }
    }

    // Blob count scales with density and canvas area. The 5000 divisor
    // was tuned so density=50 / 112² ≈ 125 blobs (medium feel; lots of
    // pockets, plenty of air space for ships to navigate).
    const std::int64_t area = static_cast<std::int64_t>(cells) * cells;
    const std::int64_t target =
        (static_cast<std::int64_t>(density) * area) / 5000;
    const std::int32_t nBlobs = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(target, 1, 256));

    std::mt19937 rng(cfg.seed);
    // Blob centers live in the upper 7/8 of the canvas (anything in
    // the floor band would be wasted work — those cells are already
    // solid). Y range = [floorYMax + 1, halfY - 2] so blobs never punch
    // through the bedrock perimeter.
    const std::int32_t blobYMin = floorYMax + 1;
    const std::int32_t blobYMax = halfY - 2;
    const std::int32_t blobXMin = -halfX + 2;
    const std::int32_t blobXMax = cells - halfX - 1 - 2;

    if (blobYMax > blobYMin && blobXMax > blobXMin) {
        std::uniform_int_distribution<std::int32_t> distX(blobXMin, blobXMax);
        std::uniform_int_distribution<std::int32_t> distY(blobYMin, blobYMax);
        std::uniform_int_distribution<std::int32_t> distRX(3, 9);
        std::uniform_int_distribution<std::int32_t> distRY(2, 6);
        for (std::int32_t i = 0; i < nBlobs; ++i) {
            const std::int32_t cx = distX(rng);
            const std::int32_t cy = distY(rng);
            const std::int32_t rX = distRX(rng);
            const std::int32_t rY = distRY(rng);
            detail::stampEllipse(grid, cx, cy, rX, rY,
                                 kProcBlobHp, info.solidCount);
        }
    }

    // Perimeter bedrock ring — 1 cell of indestructible wall so ships
    // can't escape the world even when their bounce is glitched.
    if (cfg.perimeterBedrock) {
        const std::int32_t xMin = -halfX;
        const std::int32_t xMax = cells - halfX - 1;
        const std::int32_t yMin = -(cells - halfY - 1);
        const std::int32_t yMax = halfY;
        for (std::int32_t cx = xMin; cx <= xMax; ++cx) {
            grid.setSolid(cx, yMin, 0xFFu, Attribute::Solid);
            grid.setSolid(cx, yMax, 0xFFu, Attribute::Solid);
        }
        for (std::int32_t cy = yMin; cy <= yMax; ++cy) {
            grid.setSolid(xMin, cy, 0xFFu, Attribute::Solid);
            grid.setSolid(xMax, cy, 0xFFu, Attribute::Solid);
        }
        // Recount: the perimeter ring may have just overwritten air or
        // already-solid cells. Easier to recount than diff.
        std::int32_t newCount = 0;
        for (auto a : grid.attr) {
            if (a != Attribute::Air) ++newCount;
        }
        info.solidCount = newCount;
    }

    // M5.7 — sprinkle Repair pickup tiles into Air cells. Reuses the
    // same `rng` so the same (seed, ggLevel, stuffDensity, perim,
    // repairTileCount) tuple always produces the same final grid.
    // Up to `kMaxRepairAttempts` rejection samples per requested tile
    // (most cells in a 5%-density level are Air, so the rejection
    // budget is rarely close to exhausted).
    if (cfg.repairTileCount > 0) {
        constexpr std::int32_t kMaxRepairAttempts = 32;
        const std::int32_t margin = 2;
        const std::int32_t rxMin = -halfX + margin;
        const std::int32_t rxMax = cells - halfX - 1 - margin;
        const std::int32_t ryMin = -(cells - halfY - 1) + margin;
        const std::int32_t ryMax = halfY - margin;
        if (rxMax > rxMin && ryMax > ryMin) {
            std::uniform_int_distribution<std::int32_t> rdX(rxMin, rxMax);
            std::uniform_int_distribution<std::int32_t> rdY(ryMin, ryMax);
            std::int32_t placed = 0;
            for (std::int32_t i = 0;
                 i < cfg.repairTileCount && placed < cfg.repairTileCount; ++i) {
                for (std::int32_t attempt = 0; attempt < kMaxRepairAttempts; ++attempt) {
                    const std::int32_t cx = rdX(rng);
                    const std::int32_t cy = rdY(rng);
                    if (grid.attrAt(cx, cy) != Attribute::Air) continue;
                    grid.setRepair(cx, cy);
                    ++placed;
                    ++info.solidCount;  // Repair counts as non-Air
                    break;
                }
            }
        }
    }

    info.loaded = true;
    return info;
}

} // namespace tou2d
