#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

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

    void update(threadmaxx::SystemContext& ctx) override;

    /// 3 s @ 60 Hz fixed step.
    static constexpr std::uint16_t kRespawnTicks = 180;

private:
    UserComponentIds ids_;
};

} // namespace tou2d
