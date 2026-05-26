#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>
#include <functional>

namespace tou2d {

/// Ship vs static terrain. Runs in its own wave AFTER MovementSystem so
/// it sees integrated positions and can push the ship out of solid
/// tiles by overwriting Transform + zeroing the offending velocity
/// axis.
///
/// M3.3 — terrain lives in a flat `TerrainGrid`. The system samples a
/// (2*range+1)² neighborhood around each ship's grid cell, queries the
/// grid directly (no entity walk, no hash map), and resolves any Solid
/// overlap. On a contact whose impact velocity exceeds
/// `kCrashImpactSpeed`, both the ship and the tile take a chip of
/// damage (less than a weapon hit); on tile destruction we
/// `grid->clear(cx, cy)` and fire the destroy callback inline.
///
/// reads / writes:
///   * reads  = {Transform, Velocity}
///   * writes = {Transform, Velocity, EntityStructural}
///       — EntityStructural needed for the user-component write that
///         updates `Ship::currentHp` on crash damage.
class TerrainCollisionSystem : public threadmaxx::ISystem {
public:
    TerrainCollisionSystem(UserComponentIds ids, TerrainGrid* grid) noexcept;

    const char*              name() const noexcept override { return "tou2d.collision"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
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

    /// Fires once per tile destroyed by the crash-damage path. Wired
    /// to the same host-side painter the bullet-damage path uses.
    using DestroyCallback =
        std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setDestroyCallback(DestroyCallback cb) noexcept { destroyCb_ = std::move(cb); }

private:
    UserComponentIds ids_;
    TerrainGrid*     grid_      = nullptr;   // borrowed; owned by TouGame
    DestroyCallback  destroyCb_;
};

} // namespace tou2d
