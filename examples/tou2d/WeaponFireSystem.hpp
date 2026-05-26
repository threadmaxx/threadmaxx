#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <cstdint>
#include <unordered_map>

namespace tou2d {

/// Reads PlayerInput.fireBasic + ship Transform per local-player ship,
/// spawns Dumbfire-class Bullet entities pointed along the ship's
/// current orientation. Per-ship cooldown is tracked in an internal
/// map keyed by EntityHandle — fire is rate-limited to `kFireCooldownSec`
/// (TOU's Dumbfire reload cadence).
///
/// M3.1 ships only the Dumbfire kind; other weapons land in M3.2+.
///
/// reads / writes:
///   * reads  = {Transform, Velocity, UserData}  (Velocity so the bullet
///              inherits the ship's velocity for a clean shoot-while-
///              moving feel; UserData participates as a common bit).
///   * writes = {EntityStructural}  — spawning bullets.
class WeaponFireSystem : public threadmaxx::ISystem {
public:
    explicit WeaponFireSystem(UserComponentIds ids) noexcept;

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
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

private:
    UserComponentIds                                ids_;
    /// Per-ship cooldown keyed by EntityHandle.index — stores the tick
    /// at which the ship last fired. A small std::unordered_map is fine
    /// at M3.1 scale (≤4 ships); revisit if multiplayer-with-bots
    /// pushes this hot enough to warrant a flat-vector lookup.
    std::unordered_map<std::uint32_t, std::uint64_t> lastFireTick_;
};

} // namespace tou2d
