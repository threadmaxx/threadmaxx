#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <array>
#include <cstdint>
#include <random>

namespace tou2d {

class ParticleSystem;

/// M3.3 — runs after movement / collision / weaponFire. Owns the
/// transition between alive ↔ dead ↔ respawning for every LocalPlayer
/// ship.
///
/// State machine, driven entirely by `Ship` fields:
///   * `currentHp > 0`, `respawnIn == 0` — alive; no-op.
///   * `currentHp <= 0`, `respawnIn == 0` — start death: zero velocity,
///       attach `DisabledTag` (chunk-mask flip), set `respawnIn` to
///       `kRespawnTicks`. Other systems' chunk loops skip
///       `DisabledTag` chunks, so the ship sits out the wave.
///   * `respawnIn > 1` — decrement.
///   * `respawnIn == 1` — respawn: teleport to (spawnX, spawnY), reset
///       HP/velocity, clear `DisabledTag`, zero `respawnIn`.
///
/// reads / writes:
///   * reads  = {Transform, Velocity, UserData}
///   * writes = {Transform, Velocity, EntityStructural}
///       — EntityStructural for the tag flips + user-component writes.
class ShipLifecycleSystem : public threadmaxx::ISystem {
public:
    explicit ShipLifecycleSystem(UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.shipLifecycle"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::EntityStructural,
        };
    }

    void update          (threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

    /// M4.3 — borrowed pointer to TouGame's `matchMode_`. When
    /// `LastShipStanding`, on-death stamps `respawnIn =
    /// kPermanentDeathSentinel` and the Disabled-chunk branch becomes
    /// a no-op (the ship is permanently out for the round).
    void setMatchMode(const MatchMode* mode) noexcept { matchMode_ = mode; }

    /// M4.4 — borrowed terrain grid. When set, respawn picks a random
    /// `Attribute::Air` cell from the grid instead of returning the
    /// ship to its baked-in (spawnX, spawnY). Null is fine — the
    /// respawn falls back to the original spawn point if no grid was
    /// installed (mostly affects host-side tests with a synthetic
    /// empty world).
    void setTerrainGrid(const TerrainGrid* g) noexcept { grid_ = g; }

    /// M5.3 — borrowed pointer to the demo's ParticleSystem. When set,
    /// the alive→dead transition emits a death-explosion burst at the
    /// ship's centroid in addition to the 8-ray debug-line starburst.
    /// Null is fine (host-side tests with no particles wired up).
    void setParticleSystem(ParticleSystem* p) noexcept { particles_ = p; }

    /// 3 s @ 60 Hz fixed step.
    static constexpr std::uint16_t kRespawnTicks = 180;

    /// Visual lifetime of the death starburst, in ticks. Independent of
    /// `kRespawnTicks` — the spark dies well before the ship comes
    /// back so the player sees the impact moment cleanly.
    static constexpr std::uint16_t kSparkTicks   = 36;

private:
    /// Small ring of recent death events; `buildRenderFrame` emits a
    /// fading starburst per active entry. Sized generously (8) — even
    /// 4P with simultaneous deaths only uses 4.
    struct DeathSpark {
        float          x        = 0.0f;
        float          y        = 0.0f;
        std::uint32_t  color    = 0xFFFFFFFFu;
        std::uint16_t  ticksLeft = 0;     ///< 0 = slot inactive
    };

    UserComponentIds                ids_;
    const MatchMode*                matchMode_ = nullptr;
    const TerrainGrid*              grid_      = nullptr;
    ParticleSystem*                 particles_ = nullptr;
    /// Per-system RNG for random respawn picks. Seeded with a fixed
    /// constant in the ctor so replays / smoke tests are reproducible.
    std::mt19937                    rng_{0xBA51CF11u};
    std::array<DeathSpark, 8>       sparks_{};
    std::uint32_t                   nextSpark_ = 0;
};

} // namespace tou2d
