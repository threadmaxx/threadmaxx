#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

namespace tou2d {

/// M5.8 — steers Homer-kind bullets (`Bullet::weaponKind == 10`) toward
/// the nearest enemy ship by a bounded per-tick angular step
/// (`kHomerTurnPerTickRad`). "Enemy" = any LocalPlayer ship whose slot
/// differs from the bullet's `ownerSlot`. Bullet speed is preserved
/// across the rotation so the muzzle-speed catalogue value stays the
/// stable budget.
///
/// Wave-ordering: registered between WeaponFireSystem and
/// ProjectileSystem so a freshly-spawned Homer gets its first steering
/// pass the same tick (without that, a bullet would fly straight for
/// one tick before locking on).
///
/// Cost is O(bullets * enemies); at the demo's worst case of 200 Homer
/// bullets vs 67 ships the loop is ~13 k pair tests / tick — well
/// inside budget. Non-Homer bullets early-out on the `weaponKind`
/// check, so the system pays one chunk scan + one mask check per
/// non-Homer bullet.
///
/// reads / writes:
///   * reads  = {Transform, UserData}
///   * writes = {Velocity, EntityStructural} — needed for `setVelocity`
///              and to thread the per-bullet rotation through the
///              command-buffer path.
class BulletHomingSystem : public threadmaxx::ISystem {
public:
    explicit BulletHomingSystem(UserComponentIds ids) noexcept : ids_(ids) {}

    const char*              name()   const noexcept override { return "tou2d.bulletHoming"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Velocity,
            threadmaxx::Component::EntityStructural,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// Public per-step counter — observed by `tests/tou2d_bullet_homer_test.cpp`
    /// for steering-fired smoke.
    std::uint32_t steeredThisStep() const noexcept { return steeredThisStep_; }

private:
    UserComponentIds ids_;
    std::uint32_t    steeredThisStep_ = 0;
};

} // namespace tou2d
