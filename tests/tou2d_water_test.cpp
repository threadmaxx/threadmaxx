// tou2d_water_test — pins the M7.6 Water mechanic contract.
//
//   (1) `Attribute::Water = 4` (next byte after RepairBase = 3).
//       Stable enum byte so any future snapshot / level-file format
//       can round-trip Water cells without a magic number bump.
//
//   (2) `TerrainGrid::setWater(cx, cy)` round-trips:
//       attr → Water, hp = 0 (non-blocking; bullets pass through,
//       terrain-collision sees Air-equivalent hp).
//
//   (3) Tunables (`kWaterBuoyancyFraction`, `kWaterDragPerSecond`)
//       sit in a sensible band — buoyancy in (0, 1) so a wet ship
//       still feels gravity (just dampened); drag is positive but
//       not so steep that a ship snaps to zero velocity in one tick
//       at 60 Hz.
//
//   (4) Integration semantics. A simulated step matching
//       MovementSystem's integrate order shows:
//         a) air-only ship gains exactly `-kGravityAccel * dt` per
//            tick on the Y axis,
//         b) a fully-wet ship gains LESS downward velocity in one
//            tick (buoyancy attenuates gravity),
//         c) repeated wet ticks lead to a lower terminal-fall speed
//            than an air-only ship (buoyancy + extra drag together).

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"

#include <cmath>
#include <cstdint>

int main() {
    using tou2d::Attribute;
    using tou2d::TerrainGrid;
    using tou2d::kWaterBuoyancyFraction;
    using tou2d::kWaterDragPerSecond;

    // ---- (1) Attribute::Water enum byte ---------------------------------
    static_assert(static_cast<std::uint8_t>(Attribute::Water) == 4,
                  "Attribute::Water must stay at enum value 4 — stable "
                  "byte position past RepairBase = 3 for forward-compat "
                  "with any future level/snapshot format.");
    CHECK(Attribute::Water != Attribute::Air);
    CHECK(Attribute::Water != Attribute::Solid);
    CHECK(Attribute::Water != Attribute::RepairBase);
    CHECK(Attribute::Water != Attribute::Damage);

    // ---- (2) setWater round-trips ---------------------------------------
    {
        TerrainGrid g;
        g.reset(9, 9);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Air);
        CHECK_EQ(g.hpAt(0, 0),   std::uint8_t{0});
        g.setWater(0, 0);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Water);
        // hp stays 0 — bullets and terrain-collision treat Water as Air
        // for solidity purposes; the attribute is the only signal.
        CHECK_EQ(g.hpAt(0, 0),   std::uint8_t{0});
        g.clear(0, 0);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Air);
        // Out-of-bounds setWater is a silent no-op.
        g.setWater(999, 999);
        CHECK_EQ(g.attrAt(0, 0), Attribute::Air);
    }

    // ---- (3) Tunables sit in sensible bands -----------------------------
    {
        // Buoyancy must partially offset gravity, never fully eliminate
        // it (a fully-wet ship still sinks, just slowly).
        CHECK(kWaterBuoyancyFraction > 0.0f);
        CHECK(kWaterBuoyancyFraction < 1.0f);
        // Drag must be positive (water IS draggier than air) but bounded
        // — a value > ~5 would zero velocity in fewer than 10 ticks at
        // 60 Hz which would feel like a hard wall.
        CHECK(kWaterDragPerSecond > 0.0f);
        CHECK(kWaterDragPerSecond < 5.0f);
    }

    // ---- (4) Integration semantics --------------------------------------
    // Mirror the MovementSystem step order on a single-axis dummy:
    //   v.y -= g * (1 - wetness * kBuoyancyFraction) * dt
    //   v.y *= airDamping
    //   if wetness > 0: v.y *= exp(-kWaterDrag * wetness * dt)
    constexpr float kGravity     = 120.0f;   // MovementSystem.cpp kGravityAccel
    constexpr float kAirDamping  = 0.45f;    // MovementSystem.cpp kAirDamping
    constexpr float dt           = 1.0f / 60.0f;
    const     float airDampStep  = std::exp(-kAirDamping * dt);

    auto stepY = [&](float vY, float wetness) -> float {
        const float gScale = 1.0f - wetness * kWaterBuoyancyFraction;
        vY -= kGravity * gScale * dt;
        vY *= airDampStep;
        if (wetness > 0.0f) {
            vY *= std::exp(-kWaterDragPerSecond * wetness * dt);
        }
        return vY;
    };

    // (4a) Air-only ship: one tick from rest gives the expected delta-v.
    {
        const float vAfter = stepY(0.0f, /*wetness=*/0.0f);
        const float expected = (0.0f - kGravity * dt) * airDampStep;
        CHECK(std::fabs(vAfter - expected) < 1e-5f);
        CHECK(vAfter < 0.0f);  // ship falls
    }

    // (4b) Fully-wet ship loses LESS downward speed than air-only in
    //      one tick — buoyancy attenuates gravity.
    {
        const float vAir = stepY(0.0f, 0.0f);
        const float vWet = stepY(0.0f, 1.0f);
        // Both are negative. "Less downward speed" → wet velocity is
        // strictly closer to zero (greater) than the air one.
        CHECK(vAir < 0.0f);
        CHECK(vWet < 0.0f);
        CHECK(vWet > vAir);
    }

    // (4c) Repeated wet ticks settle to a lower terminal speed than
    //      repeated air ticks. Run 600 ticks (10 s @ 60 Hz) so both
    //      reach steady state well within float tolerance.
    {
        float vAir = 0.0f, vWet = 0.0f;
        for (int i = 0; i < 600; ++i) {
            vAir = stepY(vAir, 0.0f);
            vWet = stepY(vWet, 1.0f);
        }
        // Both terminal speeds are downward; wet is smaller in
        // magnitude (closer to zero).
        CHECK(vAir < 0.0f);
        CHECK(vWet < 0.0f);
        CHECK(std::fabs(vWet) < std::fabs(vAir));
        // Wet terminal must be MEANINGFULLY slower — at least 25%
        // less downward speed than air — so a player feels the
        // difference. (Empirical with defaults: ~60% slower.)
        CHECK(std::fabs(vWet) < std::fabs(vAir) * 0.75f);
    }

    // (4d) Half-wetness sits strictly between air and full-wet — the
    //      smooth-blend contract requires monotonicity in wetness.
    {
        const float vAir  = stepY(0.0f, 0.0f);
        const float vHalf = stepY(0.0f, 0.5f);
        const float vWet  = stepY(0.0f, 1.0f);
        CHECK(vAir  < vHalf);
        CHECK(vHalf < vWet);
    }

    EXIT_WITH_RESULT();
}
