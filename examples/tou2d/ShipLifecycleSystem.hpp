#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <array>
#include <cstdint>

namespace tou2d {

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
    std::array<DeathSpark, 8>       sparks_{};
    std::uint32_t                   nextSpark_ = 0;
};

} // namespace tou2d
