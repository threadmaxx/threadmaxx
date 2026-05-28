// tou2d_proc_gen_test — pins the M5.5 procedural-level generator.
//
// Determinism contract: same (seed, ggLevel, stuffDensity,
// perimeterBedrock) → byte-identical `attr[]` and `hp[]`. Distinct
// seeds produce distinct outputs. Every ggLevel 0..4 yields a non-
// degenerate playable map (Air + Solid both nonzero). Generator is
// header-only — this test drives it directly with no engine deps.

#include "Check.hpp"

#include "../examples/tou2d/ProceduralLevel.hpp"

#include <cstdint>

namespace {

std::int32_t countAttr(const tou2d::TerrainGrid& g, tou2d::Attribute want) {
    std::int32_t n = 0;
    for (auto a : g.attr) if (a == want) ++n;
    return n;
}

bool sameGrid(const tou2d::TerrainGrid& a, const tou2d::TerrainGrid& b) {
    return a.cellsX == b.cellsX && a.cellsY == b.cellsY &&
           a.attr   == b.attr   && a.hp     == b.hp;
}

} // namespace

int main() {
    using tou2d::TerrainGrid;
    using tou2d::ProceduralLevelConfig;
    using tou2d::generateProceduralLevel;
    using tou2d::Attribute;

    // ---- Determinism: same config → identical grid -------------------------
    {
        ProceduralLevelConfig cfg{};
        cfg.seed = 0xDEADBEEFu;
        cfg.ggLevel = 2;
        cfg.stuffDensity = 50;
        cfg.perimeterBedrock = 1;

        TerrainGrid a;
        TerrainGrid b;
        const auto infoA = generateProceduralLevel(a, cfg);
        const auto infoB = generateProceduralLevel(b, cfg);
        CHECK(infoA.loaded);
        CHECK(infoB.loaded);
        CHECK_EQ(infoA.cellsX, infoB.cellsX);
        CHECK_EQ(infoA.cellsY, infoB.cellsY);
        CHECK_EQ(infoA.solidCount, infoB.solidCount);
        CHECK_EQ(infoA.seedUsed, std::uint32_t{0xDEADBEEFu});
        CHECK(sameGrid(a, b));
    }

    // ---- Determinism: post-reset reuse of the same grid lands the same state
    {
        ProceduralLevelConfig cfg{};
        cfg.seed = 42;
        cfg.ggLevel = 1;
        cfg.stuffDensity = 30;
        cfg.perimeterBedrock = 1;

        TerrainGrid a;
        generateProceduralLevel(a, cfg);
        // Mutate the grid then regenerate — the generator must reset
        // before fill so the pre-existing junk doesn't leak through.
        for (auto& cell : a.hp)   cell = 0x7F;
        for (auto& cell : a.attr) cell = Attribute::Damage;

        TerrainGrid b;
        generateProceduralLevel(a, cfg);
        generateProceduralLevel(b, cfg);
        CHECK(sameGrid(a, b));
    }

    // ---- Distinct seeds → distinct outputs --------------------------------
    {
        ProceduralLevelConfig cfg{};
        cfg.ggLevel = 2;
        cfg.stuffDensity = 50;
        cfg.perimeterBedrock = 1;

        TerrainGrid a, b;
        cfg.seed = 1;
        generateProceduralLevel(a, cfg);
        cfg.seed = 2;
        generateProceduralLevel(b, cfg);
        CHECK(!sameGrid(a, b));
    }

    // ---- Every ggLevel 0..4 yields a non-degenerate map -------------------
    {
        for (std::uint8_t lvl = 0; lvl <= 4; ++lvl) {
            ProceduralLevelConfig cfg{};
            cfg.seed = 12345;
            cfg.ggLevel = lvl;
            cfg.stuffDensity = 50;
            cfg.perimeterBedrock = 1;

            TerrainGrid g;
            const auto info = generateProceduralLevel(g, cfg);
            CHECK(info.loaded);
            CHECK_EQ(info.cellsX, tou2d::kGgLevelCells[lvl]);
            CHECK_EQ(info.cellsY, tou2d::kGgLevelCells[lvl]);
            const std::int32_t air   = countAttr(g, Attribute::Air);
            const std::int32_t solid = countAttr(g, Attribute::Solid);
            // Non-degenerate: there must be room to fly AND something
            // to crash into. The perimeter ring + floor band guarantee
            // solid > 0; the upper canvas guarantees air > 0.
            CHECK(air > 0);
            CHECK(solid > 0);
            CHECK_EQ(info.solidCount, solid);
        }
    }

    // ---- Density extremes: 0 → only floor + perimeter; 100 → busier -------
    {
        ProceduralLevelConfig cfg{};
        cfg.seed = 7;
        cfg.ggLevel = 2;
        cfg.perimeterBedrock = 1;

        TerrainGrid loDensity, hiDensity;
        cfg.stuffDensity = 0;
        const auto loInfo = generateProceduralLevel(loDensity, cfg);
        cfg.stuffDensity = 100;
        const auto hiInfo = generateProceduralLevel(hiDensity, cfg);

        // Density 0 still draws 1 blob (the floor clamps to >= 1), but
        // density 100 must produce strictly more solid cells.
        CHECK(loInfo.solidCount < hiInfo.solidCount);
    }

    // ---- Perimeter toggle ------------------------------------------------
    {
        ProceduralLevelConfig cfg{};
        cfg.seed = 11;
        cfg.ggLevel = 0;
        cfg.stuffDensity = 0;

        TerrainGrid withPerim, noPerim;
        cfg.perimeterBedrock = 1;
        generateProceduralLevel(withPerim, cfg);
        cfg.perimeterBedrock = 0;
        generateProceduralLevel(noPerim, cfg);

        // The perimeter version must have the 4 corner cells as solid
        // bedrock; the no-perim version must not.
        const std::int32_t halfX = withPerim.cellsX / 2;
        const std::int32_t halfY = withPerim.cellsY / 2;
        const std::int32_t cornerX = -halfX;
        const std::int32_t cornerY = halfY;
        CHECK_EQ(withPerim.hpAt(cornerX, cornerY), std::uint8_t{0xFF});
        CHECK_EQ(noPerim.hpAt  (cornerX, cornerY), std::uint8_t{0});
    }

    // ---- Out-of-range ggLevel is clamped to 4 -----------------------------
    {
        ProceduralLevelConfig cfg{};
        cfg.seed = 99;
        cfg.ggLevel = 99;
        cfg.stuffDensity = 200;  // also out of range
        cfg.perimeterBedrock = 1;

        TerrainGrid g;
        const auto info = generateProceduralLevel(g, cfg);
        CHECK(info.loaded);
        CHECK_EQ(info.cellsX, tou2d::kGgLevelCells[4]);
        CHECK_EQ(info.cellsY, tou2d::kGgLevelCells[4]);
    }

    EXIT_WITH_RESULT();
}
