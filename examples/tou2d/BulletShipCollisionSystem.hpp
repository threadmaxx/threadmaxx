#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace threadmaxx { class Engine; }

namespace tou2d {

class ParticleSystem;

/// M3.5 — bullet-vs-ship hit detection + damage + frag credit.
///
/// Runs BEFORE `BulletTerrainSystem` in registration order so a bullet
/// that hits a ship doesn't also get charged with a tile destroy that
/// same tick. Both are single-thread `ctx.single` systems on the same
/// wave (writes={EntityStructural}); commit order = registration order,
/// so this one's bullet `destroy()` calls land first.
///
/// Hit model: each live (LocalPlayer && !DisabledTag) ship has a
/// world-space hit radius derived from its `transform.scale`. For every
/// bullet, scan up to 4 ships, point-in-circle test against the
/// bullet's centroid. Same chunk as the shooter? Friendly-fire is OFF
/// (a bullet whose `ownerSlot` matches the ship's `slot` is ignored —
/// matches the original TOU). On a hit:
///   * Ship HP -= bullet.damage. The bullet is destroyed.
///   * If the hit drops HP to ≤ 0 AND the ship was alive AND the
///     shooter is not the victim, the shooter's `kills++`. If that
///     count crosses `kFragLimit` (Deathmatch mode), emit RoundEnded.
///
/// M4.3 — match-mode-aware round-end:
///   * Deathmatch (default): emit when any shooter's kills crosses
///     `kFragLimit`. Existing behavior.
///   * LastShipStanding: after damage application, count ships with
///     `currentHp > 0`. If ≤ 1 the round ends; winner = surviving slot,
///     or (on mutual annihilation) the slot with the most kills.
///
/// Round-end state is the shared atomic owned by `TouGame`. Collision
/// is now the authoritative writer (no internal mirror bool). When the
/// atomic is true (set either by collision or by `RoundRestartSystem`'s
/// reset), no further emissions happen until it goes false again. The
/// `RoundRestartSystem` flips it false, re-arming the gate.
///
/// reads / writes:
///   * reads  = {Transform, Velocity, UserData}
///   * writes = {EntityStructural}
///       — structural because `cb.destroy(bullet)` and user-component
///         writes both flow through it.
class BulletShipCollisionSystem : public threadmaxx::ISystem {
public:
    /// `engine` is borrowed; must outlive the system. Used for the
    /// `events<RoundEnded>()` emit + `logger()` access (no
    /// `SystemContext::engine()` accessor exists today).
    BulletShipCollisionSystem(UserComponentIds ids,
                              threadmaxx::Engine* engine) noexcept;

    const char*              name()   const noexcept override { return "tou2d.bulletShipCollision"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::EntityStructural,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// M4.3 — install the shared round-end latch + the borrowed winner
    /// pointers (TouGame owns both). When the round ends, this system
    /// writes both directly so the rest of the engine sees consistent
    /// post-condition before the next tick's preStep gates fire.
    void setRoundEndedFlag(std::shared_ptr<std::atomic<bool>> f,
                           std::uint8_t*  winnerSlot,
                           std::uint16_t* winnerKills) noexcept {
        roundEnded_  = std::move(f);
        winnerSlot_  = winnerSlot;
        winnerKills_ = winnerKills;
    }

    /// M4.3 — borrowed pointer to TouGame's `matchMode_` member. Read
    /// once per update() so a future toggle (e.g. from a debug menu)
    /// takes effect on the next tick without restarting the engine.
    void setMatchMode(const MatchMode* mode) noexcept { matchMode_ = mode; }

    /// M5.3 — borrowed pointer to the demo's ParticleSystem. When set,
    /// every bullet-ship intersection emits a 5-particle spark burst
    /// at the bullet's centroid (its `(bx, by)` at the moment of
    /// detection). Null is fine.
    void setParticleSystem(ParticleSystem* p) noexcept { particles_ = p; }

    /// N4 (2026-06-18) — Settings::gameplay.damageScale read at restart.
    /// Multiplies every bullet's `Bullet::damage` byte when computing
    /// the victim's HP delta. Defaults to 1.0 (no change). Clamped to
    /// [0.0, 10.0] defensively — the settings UI exposes a 0.5/1.0/1.5/2.0
    /// preset row, but a hand-edited settings.dat with a wild value
    /// can't blow up the simulation.
    void setDamageScale(float scale) noexcept {
        damageScale_ = scale < 0.0f ? 0.0f
                     : scale > 10.0f ? 10.0f : scale;
    }
    float damageScale() const noexcept { return damageScale_; }

private:
    UserComponentIds                   ids_;
    threadmaxx::Engine*                engine_      = nullptr;
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    std::uint8_t*                      winnerSlot_  = nullptr;
    std::uint16_t*                     winnerKills_ = nullptr;
    const MatchMode*                   matchMode_   = nullptr;
    ParticleSystem*                    particles_   = nullptr;
    float                              damageScale_ = 1.0f;   // N4
};

} // namespace tou2d
