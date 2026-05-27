#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>
#include <functional>

namespace threadmaxx { class Engine; }

namespace tou2d {

/// Bullet vs static terrain. Runs in its own wave AFTER
/// ProjectileSystem so it sees integrated bullet positions.
///
/// M3.3 — terrain lives in a flat `TerrainGrid` (cellsX × cellsY bytes
/// for HP plus the same for attribute), not per-tile entities. Bullet
/// hit-test is a direct `grid->hpAt(cx, cy)` lookup; on destruction we
/// `grid->clear(cx, cy)` and fire the destroy callback.
///
/// reads / writes:
///   * reads  = {Transform}  (bullet positions)
///   * writes = {EntityStructural}  — destroys spent bullets.
class BulletTerrainSystem : public threadmaxx::ISystem {
public:
    BulletTerrainSystem(UserComponentIds ids, TerrainGrid* grid,
                        threadmaxx::Engine* engine = nullptr) noexcept;

    const char*              name()   const noexcept override { return "tou2d.bulletTerrain"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::EntityStructural,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// Fires once per tile actually destroyed (HP wraps past zero).
    /// Host code wires this to the background-bitmap painter.
    using DestroyCallback = std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setDestroyCallback(DestroyCallback cb) noexcept { destroyCb_ = std::move(cb); }

private:
    UserComponentIds    ids_;
    TerrainGrid*        grid_   = nullptr;   // borrowed; owned by TouGame
    threadmaxx::Engine* engine_ = nullptr;   // borrowed; AudioPlay only
    DestroyCallback     destroyCb_;
};

} // namespace tou2d
