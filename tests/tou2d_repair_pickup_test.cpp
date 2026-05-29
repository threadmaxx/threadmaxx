// tou2d_repair_pickup_test — pins the M5.7 repair-tile pickup contract.
//
// Contract:
//   * `Attribute::Repair` is a distinct, non-blocking attribute. It is
//     NOT `Solid`, so existing terrain-collision code that filters on
//     `Solid` keeps passing the ship through repair cells.
//   * `TerrainGrid::setRepair` writes Attribute::Repair with a non-zero
//     HP sentinel; `clear` flips back to Air.
//   * The cycle rule `specialKind = (specialKind + 1) % kSpecialKindCount`
//     visits every kind exactly once across one full revolution.
//   * Healing clamps to `Ship::maxHp` so a full-HP ship still
//     consumes the tile (the original game's behaviour — the player
//     grabs it for the weapon-cycle effect even when at full health).
//   * Procedural generator with `repairTileCount = N` produces a grid
//     containing exactly N `Attribute::Repair` cells, and the same
//     config reproduces the exact same placement (determinism).

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/ProceduralLevel.hpp"

#include <algorithm>
#include <cstdint>

namespace {

std::int32_t countAttr(const tou2d::TerrainGrid& g, tou2d::Attribute want) {
    std::int32_t n = 0;
    for (auto a : g.attr) if (a == want) ++n;
    return n;
}

} // namespace

int main() {
    using tou2d::Attribute;
    using tou2d::TerrainGrid;
    using tou2d::kSpecialKindCount;
    using tou2d::ProceduralLevelConfig;
    using tou2d::generateProceduralLevel;

    // ---- Attribute is distinct -------------------------------------------
    static_assert(static_cast<std::uint8_t>(Attribute::Repair) == 3,
                  "Repair must stay at enum value 3 — round-trips through "
                  "ProceduralLevelConfig and snapshots");
    CHECK(Attribute::Repair != Attribute::Solid);
    CHECK(Attribute::Repair != Attribute::Air);

    // ---- setRepair / clear round-trip ------------------------------------
    {
        TerrainGrid g;
        g.reset(9, 9);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Air);
        g.setRepair(0, 0);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Repair);
        CHECK(g.hpAt(0, 0) > 0);
        g.clear(0, 0);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Air);
        CHECK_EQ(g.hpAt(0, 0),   std::uint8_t{0});
    }

    // ---- Cycle visits every special kind exactly once --------------------
    {
        std::uint8_t k = 0;
        std::array<bool, 16> visited{};  // 16 > kSpecialKindCount
        for (std::uint8_t i = 0; i < kSpecialKindCount; ++i) {
            CHECK(!visited[k]);
            visited[k] = true;
            k = static_cast<std::uint8_t>((k + 1u) % kSpecialKindCount);
        }
        CHECK_EQ(k, std::uint8_t{0});  // wraps back to Spread
        for (std::uint8_t i = 0; i < kSpecialKindCount; ++i) {
            CHECK(visited[i]);
        }
    }

    // ---- Heal clamps to maxHp --------------------------------------------
    {
        tou2d::Ship sh{};
        sh.maxHp     = 150.0f;
        sh.currentHp = 140.0f;
        const float healed = std::min(
            sh.maxHp,
            sh.currentHp + tou2d::kRepairHealAmount);
        CHECK_EQ(healed, sh.maxHp);  // clamped at full
        // From a non-full state, heal increases.
        sh.currentHp = 50.0f;
        const float healed2 = std::min(
            sh.maxHp,
            sh.currentHp + tou2d::kRepairHealAmount);
        CHECK(healed2 > sh.currentHp);
        CHECK(healed2 <= sh.maxHp);
    }

    // ---- Procedural generator sprinkles requested count ------------------
    {
        ProceduralLevelConfig cfg{};
        cfg.seed             = 0xCAFEBABEu;
        cfg.ggLevel          = 2;
        cfg.stuffDensity     = 50;
        cfg.perimeterBedrock = 1;
        cfg.repairTileCount  = 12;

        TerrainGrid a, b;
        const auto infoA = generateProceduralLevel(a, cfg);
        const auto infoB = generateProceduralLevel(b, cfg);
        CHECK(infoA.loaded);
        CHECK(infoB.loaded);
        // Determinism: same config → same grid.
        CHECK(a.attr == b.attr);
        CHECK(a.hp   == b.hp);
        // Exactly N Repair cells (the random sampler is given enough
        // attempts that this is reliable on a 112x112 medium canvas
        // with ~50% air). If the test ever flakes here, bump
        // kMaxRepairAttempts in ProceduralLevel.hpp.
        CHECK_EQ(countAttr(a, Attribute::Repair), std::int32_t{12});
    }

    // ---- repairTileCount == 0 → no Repair cells (pre-M5.7 path) ----------
    {
        ProceduralLevelConfig cfg{};
        cfg.seed            = 0xCAFEBABEu;
        cfg.ggLevel         = 2;
        cfg.stuffDensity    = 50;
        cfg.perimeterBedrock= 1;
        cfg.repairTileCount = 0;  // default — preserves pre-M5.7 behaviour

        TerrainGrid g;
        generateProceduralLevel(g, cfg);
        CHECK_EQ(countAttr(g, Attribute::Repair), std::int32_t{0});
    }

    // ---- Distinct repairTileCount produces a distinct grid --------------
    // Same seed but different repair count → different placements (the
    // sprinkle consumes RNG draws even when zero, but stepping the
    // count flips the cell layout regardless).
    {
        ProceduralLevelConfig cfg{};
        cfg.seed = 0x55667788u;
        cfg.ggLevel = 2;
        cfg.repairTileCount = 4;
        TerrainGrid a;
        generateProceduralLevel(a, cfg);
        cfg.repairTileCount = 16;
        TerrainGrid b;
        generateProceduralLevel(b, cfg);
        CHECK(countAttr(a, Attribute::Repair) != countAttr(b, Attribute::Repair));
    }

    EXIT_WITH_RESULT();
}
