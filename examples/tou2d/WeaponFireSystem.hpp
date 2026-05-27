#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <atomic>
#include <memory>

namespace threadmaxx { class Engine; }

namespace tou2d {

/// Reads PlayerInput.fireBasic + ship Transform per local-player ship,
/// spawns Bullet entities pointed along the ship's current orientation.
///
/// M4.2 — per-ship ammo + reload via the `WeaponLoadout` user component
/// (lives on the same entity as the ship). The system reads the
/// loadout, gates fires on `ammo > 0 && reloadIn == 0`, decrements
/// ammo on each fire, sets `reloadIn = kReload*Ticks` the moment the
/// magazine hits zero, and ticks the reload counter back to zero each
/// step. A reload that completes this tick refills ammo to the
/// magazine size — but does NOT fire the same tick (the player has to
/// re-press / re-hold to start the next burst). The previous
/// per-`EntityHandle.index` cooldown maps are gone; the loadout
/// component now carries the rate-limit state.
///
/// reads / writes:
///   * reads  = {Transform, Velocity, UserData}  (Velocity so the bullet
///              inherits the ship's velocity for a clean shoot-while-
///              moving feel; UserData participates as a common bit).
///   * writes = {EntityStructural, UserData}  — spawning bullets +
///              rewriting WeaponLoadout each tick.
class WeaponFireSystem : public threadmaxx::ISystem {
public:
    /// `engine` borrowed for `events<AudioPlay>` emission only (M4.8).
    /// Optional — pass nullptr to suppress audio cues.
    WeaponFireSystem(UserComponentIds ids,
                     threadmaxx::Engine* engine = nullptr) noexcept;

    const char*              name()   const noexcept override { return "tou2d.weaponFire"; }
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
            threadmaxx::Component::UserData,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// M4.2 — round-end shared latch. When set, update early-outs so
    /// no bullets spawn for the duration of the freeze; reload
    /// counters also stop ticking (consistent with the world's idea
    /// of "frozen", and prevents post-round phantom refills).
    void setRoundEndedFlag(std::shared_ptr<std::atomic<bool>> f) noexcept {
        roundEnded_ = std::move(f);
    }

private:
    UserComponentIds                   ids_;
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    threadmaxx::Engine*                engine_ = nullptr;   // borrowed; AudioPlay emit only
};

} // namespace tou2d
