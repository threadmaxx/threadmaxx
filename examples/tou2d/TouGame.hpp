#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>

struct GLFWwindow;

namespace tou2d {

class BulletTerrainSystem;
class CameraSystem;
class TerrainCollisionSystem;

/// IGame implementation for the tou2d demo.
///
/// onSetup:
///   * Registers user components (PlayerInput, LocalPlayer, Ship,
///     Bullet — no per-tile entity in M3.3+; terrain lives in
///     `TerrainGrid grid_`).
///   * Registers Input / Movement / Collision / WeaponFire /
///     Projectile / BulletTerrain / ShipLifecycle / Camera.
///   * Populates `grid_` from either an imported `.lev` directory or
///     the synthetic arena fallback.
///   * Spawns 4 ships, one per LocalPlayer slot (P2-P4 share the
///     keyboard; physically present even when nobody's holding the
///     keys — they just sit at their spawn point).
class TouGame : public threadmaxx::IGame {
public:
    explicit TouGame(GLFWwindow* window) noexcept;

    void setLevelDir(std::filesystem::path p) noexcept { levelDir_ = std::move(p); }

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&  world,
                 threadmaxx::CommandBuffer& seed) override;
    void onTeardown(threadmaxx::Engine& engine,
                    threadmaxx::World&  world) override;

    CameraSystem* cameraSystem() noexcept { return camera_; }

    /// Handle of P1's ship — host-side smoke tests use this to verify
    /// final position. Valid only between onSetup and onTeardown.
    threadmaxx::EntityHandle playerShip() const noexcept { return playerShips_[0]; }

    std::int32_t levelCellsX() const noexcept { return cellsX_; }
    std::int32_t levelCellsY() const noexcept { return cellsY_; }

    /// Fires once per destroyed tile from either destruction path.
    using TileDestroyCallback =
        std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setTileDestroyCallback(TileDestroyCallback cb);

private:
    GLFWwindow*              window_         = nullptr;
    UserComponentIds         ids_;
    CameraSystem*            camera_         = nullptr;   // borrowed
    BulletTerrainSystem*     bulletTerrain_  = nullptr;   // borrowed
    TerrainCollisionSystem*  collision_      = nullptr;   // borrowed
    std::array<threadmaxx::EntityHandle, 4> playerShips_{};
    std::filesystem::path    levelDir_;
    std::int32_t             cellsX_         = 0;
    std::int32_t             cellsY_         = 0;
    TerrainGrid              grid_;
};

} // namespace tou2d
