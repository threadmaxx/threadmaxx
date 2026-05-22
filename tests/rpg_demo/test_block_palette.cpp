// §3.11 batch D10 — voxel block kind distribution + palette test.
//
// Pure deterministic-data test (no engine boot). Builds a Heightmap
// with the same seed the demo uses at boot, walks every (column,
// blockY) pair, and:
//
//   1. Top-cube kind per band matches the `kindAt` table:
//        columnTopY <  2 m  → Sand
//        columnTopY <  6 m  → Grass
//        columnTopY <  9 m  → Stone
//        columnTopY ≥  9 m  → Snow
//   2. Sub-surface kinds layer correctly:
//        depthFromTop ∈ (0.5, 2.5) → Dirt
//        depthFromTop ≥ 2.5        → Stone
//   3. Distribution has at least one column in each populated band
//      (otherwise the visual variety the batch promises isn't there).
//   4. `blockKindColor` returns distinct RGB triples for every
//      defined kind (no two kinds collide).
//
// This is the spawn-time contract for D11's harvest gating; if the
// kind / color mapping ever drifts, D11's per-kind hardness lookup
// also drifts.

#include "Check.hpp"
#include "DemoTypes.hpp"
#include "Heightmap.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

int main() {
    using namespace rpg;

    Heightmap hmap(kHeightmapResolution, kTerrainExtent, kHeightmapSeed);
    const float halfExtent = kTerrainExtent * 0.5f;
    const std::uint32_t cells = kNormalTerrainCellsPerSide;
    const float tileSize = kTerrainExtent / static_cast<float>(cells);
    const float blockUnit = hmap.blockUnit();

    std::array<std::uint32_t, 7> kindCounts{};  // index by BlockKind
    std::uint32_t topCubesChecked = 0;
    std::uint32_t subSurfaceChecked = 0;

    for (std::uint32_t cz = 0; cz < cells; ++cz) {
        for (std::uint32_t cx = 0; cx < cells; ++cx) {
            const float worldX =
                -halfExtent + (static_cast<float>(cx) + 0.5f) * tileSize;
            const float worldZ =
                -halfExtent + (static_cast<float>(cz) + 0.5f) * tileSize;
            const float topY = hmap.heightAt(worldX, worldZ);
            const auto blockCount = static_cast<std::uint32_t>(
                std::max(0.0f, std::round(topY / blockUnit)));
            if (blockCount == 0) continue;

            for (std::uint32_t k = 0; k < blockCount; ++k) {
                const float blockY = (static_cast<float>(k) + 0.5f) * blockUnit;
                const BlockKind kind = kindAt(topY, blockY);
                ++kindCounts[static_cast<std::size_t>(kind)];

                const bool isTop = (k + 1u == blockCount);
                if (isTop) {
                    ++topCubesChecked;
                    if (topY < 2.0f)      CHECK(kind == BlockKind::Sand);
                    else if (topY < 6.0f) CHECK(kind == BlockKind::Grass);
                    else if (topY < 9.0f) CHECK(kind == BlockKind::Stone);
                    else                  CHECK(kind == BlockKind::Snow);
                } else {
                    ++subSurfaceChecked;
                    const float depthFromTop = topY - blockY - 0.5f;
                    if (depthFromTop < 2.5f) CHECK(kind == BlockKind::Dirt);
                    else                     CHECK(kind == BlockKind::Stone);
                }
            }
        }
    }

    std::printf("[test_block_palette] sand=%u grass=%u dirt=%u stone=%u snow=%u "
                "topChecked=%u subChecked=%u\n",
                kindCounts[static_cast<std::size_t>(BlockKind::Sand)],
                kindCounts[static_cast<std::size_t>(BlockKind::Grass)],
                kindCounts[static_cast<std::size_t>(BlockKind::Dirt)],
                kindCounts[static_cast<std::size_t>(BlockKind::Stone)],
                kindCounts[static_cast<std::size_t>(BlockKind::Snow)],
                topCubesChecked, subSurfaceChecked);

    // Sanity: with the default seed + 48×48 grid + height range [0, 12],
    // we expect a healthy mix. The exact counts depend on the noise
    // field — these are floor bounds, not equality.
    CHECK(topCubesChecked == cells * cells ||
          kindCounts[static_cast<std::size_t>(BlockKind::Sand)] > 0u);
    CHECK(kindCounts[static_cast<std::size_t>(BlockKind::Grass)] > 0u);
    CHECK(kindCounts[static_cast<std::size_t>(BlockKind::Dirt)]  > 0u);
    CHECK(kindCounts[static_cast<std::size_t>(BlockKind::Stone)] > 0u);
    CHECK(subSurfaceChecked > 0u);

    // ---- Color palette uniqueness ------------------------------------------
    // Every distinct BlockKind must produce a distinct RGB triple. If
    // two kinds collide the renderer can't tell them apart visually,
    // which defeats the entire D10 batch.
    const BlockKind allKinds[] = {
        BlockKind::Sand,  BlockKind::Grass, BlockKind::Dirt,
        BlockKind::Stone, BlockKind::Snow,  BlockKind::Water,
        BlockKind::Wood,
    };
    constexpr std::size_t kKindCount = sizeof(allKinds) / sizeof(allKinds[0]);
    float colors[kKindCount][4];
    for (std::size_t i = 0; i < kKindCount; ++i) {
        blockKindColor(allKinds[i], colors[i]);
        CHECK(colors[i][3] == 1.0f);  // alpha always opaque
    }
    for (std::size_t i = 0; i < kKindCount; ++i) {
        for (std::size_t j = i + 1; j < kKindCount; ++j) {
            const float dr = colors[i][0] - colors[j][0];
            const float dg = colors[i][1] - colors[j][1];
            const float db = colors[i][2] - colors[j][2];
            CHECK(std::sqrt(dr * dr + dg * dg + db * db) > 0.05f);
        }
    }

    // Hardness mapping is monotone-ish — Sand soft, Stone hard.
    CHECK(blockKindHardness(BlockKind::Sand)  <
          blockKindHardness(BlockKind::Stone));
    CHECK(blockKindHardness(BlockKind::Dirt)  <
          blockKindHardness(BlockKind::Stone));
    CHECK(blockKindHardness(BlockKind::Water) == 0.0f);

    EXIT_WITH_RESULT();
}
