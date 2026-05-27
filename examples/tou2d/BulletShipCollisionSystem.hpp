#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

namespace threadmaxx { class Engine; }

namespace tou2d {

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
///     count crosses `kFragLimit`, emit a `RoundEnded` event on the
///     typed channel (once per session — re-emission is suppressed).
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

private:
    UserComponentIds    ids_;
    threadmaxx::Engine* engine_     = nullptr;
    bool                roundEnded_ = false;   ///< once true, no further RoundEnded emits
};

} // namespace tou2d
