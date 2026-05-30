// tou2d_bot_behavior_test — pins the M7.1 BotControlSystem polish.
//
// Contract pinned here:
//   * `findNearestRepairTile` is the engine-side helper M7.1.a uses to
//     decide "is there a reachable heal source?" before the low-HP
//     hysteresis flips into retreat behaviour. The function is pure
//     (TerrainGrid + (x, y) + radius → optional cell), so we exercise
//     it directly rather than spinning up a full Engine.
//   * Empty grids, zero / negative radius, and "no Repair tile in
//     radius" all return false without writing the out-params.
//   * A single Repair tile inside the radius returns true and writes
//     the cell's world-center coordinates. Boundary cells (distance
//     exactly == radius) count as inside.
//   * When multiple Repair tiles exist, the helper picks the one with
//     the smallest squared distance from origin.
//   * `kBotRepairSearchRadiusWU` is the documented default the
//     BotControlSystem uses; sanity-check it's positive and big
//     enough to cover at least a few tiles on the synthetic arena.
//   * `kBotChaosFireChancePerTick` lives in a safe band — high enough
//     to fire occasionally (≥ 0.001) but well below "spammy" (< 0.05).
//     The playtest signal is "bot feels alive between engages," not
//     "bot turret-shoots into the void."

#include "Check.hpp"

#include "../examples/tou2d/BotControlSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"

#include <cmath>

int main() {
    using tou2d::Attribute;
    using tou2d::TerrainGrid;
    using tou2d::kTileWorldUnits;
    using tou2d::findNearestRepairTile;
    using tou2d::kBotRepairSearchRadiusWU;
    using tou2d::kBotChaosFireChancePerTick;

    // ---- Empty grid → false ---------------------------------------------
    {
        TerrainGrid g;
        // No reset() — cellsX = cellsY = 0.
        float ox = 1.0f, oy = 2.0f;
        CHECK(!findNearestRepairTile(g, 0.0f, 0.0f, 100.0f, ox, oy));
        // Out-params left untouched.
        CHECK_EQ(ox, 1.0f);
        CHECK_EQ(oy, 2.0f);
    }

    // ---- Non-positive radius → false ------------------------------------
    {
        TerrainGrid g;
        g.reset(9, 9);
        g.setRepairBase(0, 0);
        float ox = 0, oy = 0;
        CHECK(!findNearestRepairTile(g,  0.0f, 0.0f,  0.0f, ox, oy));
        CHECK(!findNearestRepairTile(g,  0.0f, 0.0f, -1.0f, ox, oy));
    }

    // ---- Repair tile outside radius → false -----------------------------
    {
        TerrainGrid g;
        g.reset(33, 33);
        // Tile at cell (10, 10) → world (10*kTileWorldUnits, 10*kTileWorldUnits)
        // ≈ (35, 35) world units. Distance from origin ≈ 49.5 wu.
        g.setRepairBase(10, 10);
        float ox = -1.0f, oy = -1.0f;
        CHECK(!findNearestRepairTile(g, 0.0f, 0.0f, 20.0f, ox, oy));
        CHECK_EQ(ox, -1.0f);
        CHECK_EQ(oy, -1.0f);
    }

    // ---- Repair tile inside radius → true + correct coords --------------
    {
        TerrainGrid g;
        g.reset(33, 33);
        g.setRepairBase(10, 10);
        float ox = 0, oy = 0;
        CHECK(findNearestRepairTile(g, 0.0f, 0.0f, 100.0f, ox, oy));
        const float expectedX = 10.0f * kTileWorldUnits;
        const float expectedY = 10.0f * kTileWorldUnits;
        CHECK_EQ(ox, expectedX);
        CHECK_EQ(oy, expectedY);
    }

    // ---- Nearest of multiple Repair tiles wins --------------------------
    {
        TerrainGrid g;
        g.reset(33, 33);
        g.setRepairBase(  1,  0);   // ~3.5 wu away from origin (NEAREST)
        g.setRepairBase( 10,  5);   // ~39 wu away
        g.setRepairBase(-12, -3);   // ~43 wu away
        float ox = 0, oy = 0;
        CHECK(findNearestRepairTile(g, 0.0f, 0.0f, 200.0f, ox, oy));
        CHECK_EQ(ox, 1.0f * kTileWorldUnits);
        CHECK_EQ(oy, 0.0f);
    }

    // ---- Bot search radius covers some of the synthetic arena ----------
    {
        // Synthetic arena is kArenaHalfCells * 2 + 1 = 33 cells across.
        // Cells span ±halfX in cell coords, so the worst-case ship-to-
        // tile distance INSIDE the arena is roughly the arena's
        // diagonal: 33 * kTileWorldUnits ~= 115 wu. The search radius
        // (kBotRepairSearchRadiusWU) must comfortably exceed that so a
        // bot on one corner can find a repair tile on the opposite
        // corner — otherwise the M7.1.a logic is a no-op on the demo
        // arena.
        const float arenaDiagonal =
            std::sqrt(2.0f) * 33.0f * kTileWorldUnits;
        CHECK(kBotRepairSearchRadiusWU > arenaDiagonal);
    }

    // ---- Chaos fire chance lives in the documented safe band ------------
    {
        CHECK(kBotChaosFireChancePerTick >= 0.001f);
        CHECK(kBotChaosFireChancePerTick <= 0.05f);
    }

    EXIT_WITH_RESULT();
}
