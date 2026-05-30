// tou2d_repair_pickup_test — pins the M5.7 + M7.5 RepairBase contract.
//
// M7.5 reworked the on-touch semantics from "consume on overlap" to
// "non-consuming regen + entry-edge cycle". This test pins:
//   * `Attribute::RepairBase` (renamed from M5.7's `Attribute::Repair`)
//     is a distinct, non-blocking attribute at the same enum value
//     (3) so existing snapshots stay loadable.
//   * `TerrainGrid::setRepairBase` writes Attribute::RepairBase with a
//     non-zero HP sentinel; `clear` flips back to Air.
//   * The cycle rule `specialKind = (specialKind + 1) % kSpecialKindCount`
//     visits every kind exactly once across one full revolution.
//   * Per-tick regen at `kRepairBaseHpPerTick` clamps to `Ship::maxHp`.
//   * Procedural generator with `repairTileCount = N` produces a grid
//     containing exactly N `Attribute::RepairBase` cells, and the same
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

    // ---- Attribute is distinct (M7.5 — renamed from Repair) --------------
    static_assert(static_cast<std::uint8_t>(Attribute::RepairBase) == 3,
                  "RepairBase must stay at enum value 3 — round-trips through "
                  "ProceduralLevelConfig and pre-M7.5 snapshots");
    CHECK(Attribute::RepairBase != Attribute::Solid);
    CHECK(Attribute::RepairBase != Attribute::Air);

    // ---- setRepairBase / clear round-trip --------------------------------
    {
        TerrainGrid g;
        g.reset(9, 9);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Air);
        g.setRepairBase(0, 0);
        CHECK_EQ(g.attrAt(0, 0), Attribute::RepairBase);
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

    // ---- M7.5 — Per-tick regen at kRepairBaseHpPerTick, clamped at maxHp -
    {
        tou2d::Ship sh{};
        sh.maxHp     = 150.0f;
        sh.currentHp = 149.5f;
        const float healed = std::min(
            sh.maxHp,
            sh.currentHp + tou2d::kRepairBaseHpPerTick);
        CHECK_EQ(healed, sh.maxHp);  // clamped at full
        // From a non-full state, regen increases by exactly the per-tick
        // step (well clear of maxHp so no clamp).
        sh.currentHp = 50.0f;
        const float healed2 = std::min(
            sh.maxHp,
            sh.currentHp + tou2d::kRepairBaseHpPerTick);
        CHECK(healed2 > sh.currentHp);
        CHECK(healed2 <= sh.maxHp);
        CHECK_EQ(healed2, sh.currentHp + tou2d::kRepairBaseHpPerTick);
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
        // Exactly N RepairBase cells (the random sampler is given enough
        // attempts that this is reliable on a 112x112 medium canvas
        // with ~50% air). If the test ever flakes here, bump
        // kMaxRepairAttempts in ProceduralLevel.hpp.
        CHECK_EQ(countAttr(a, Attribute::RepairBase), std::int32_t{12});
    }

    // ---- repairTileCount == 0 → no RepairBase cells (pre-M5.7 path) ------
    {
        ProceduralLevelConfig cfg{};
        cfg.seed            = 0xCAFEBABEu;
        cfg.ggLevel         = 2;
        cfg.stuffDensity    = 50;
        cfg.perimeterBedrock= 1;
        cfg.repairTileCount = 0;  // default — preserves pre-M5.7 behaviour

        TerrainGrid g;
        generateProceduralLevel(g, cfg);
        CHECK_EQ(countAttr(g, Attribute::RepairBase), std::int32_t{0});
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
        CHECK(countAttr(a, Attribute::RepairBase) != countAttr(b, Attribute::RepairBase));
    }

    EXIT_WITH_RESULT();
}
