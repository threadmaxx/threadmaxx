#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>

struct GLFWwindow;

namespace tou2d {

class BulletTerrainSystem;
class CameraSystem;
class TerrainCollisionSystem;

/// IGame implementation for the M1 thrust-loop proof + M2 synthetic
/// arena.
///
/// onSetup:
///   * Registers user components.
///   * Registers InputSystem (preStep), MovementSystem, CameraSystem.
///   * Spawns one ship at world origin with LocalPlayer slot=0,
///     Transform + Velocity + (initially zero) PlayerInput + a meshId
///     that the renderer fills with its default unit cube.
///   * Spawns a 33×33 synthetic arena (perimeter walls + floor).
class TouGame : public threadmaxx::IGame {
public:
    explicit TouGame(GLFWwindow* window) noexcept;

    /// Optional level directory (produced by `tou2d_import_lev`). If
    /// set BEFORE `Engine::initialize`, onSetup loads the imported grid
    /// instead of spawning the synthetic arena.
    void setLevelDir(std::filesystem::path p) noexcept { levelDir_ = std::move(p); }

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&  world,
                 threadmaxx::CommandBuffer& seed) override;
    void onTeardown(threadmaxx::Engine& engine,
                    threadmaxx::World&  world) override;

    /// Borrowed; set after Engine::initialize so the host can forward
    /// resize events to the camera. CameraSystem owns the viewport.
    CameraSystem* cameraSystem() noexcept { return camera_; }

    /// Handle of P1's ship — host-side smoke tests use this to verify
    /// final position post-shutdown. Valid only between onSetup and
    /// onTeardown.
    threadmaxx::EntityHandle playerShip() const noexcept { return playerShip_; }

    /// Loaded-level dimensions in tile units, populated during onSetup
    /// if a `--level` directory was provided. Zero when the synthetic
    /// arena fallback ran instead.
    std::int32_t levelCellsX() const noexcept { return cellsX_; }
    std::int32_t levelCellsY() const noexcept { return cellsY_; }

    /// Fires once per destroyed tile (BulletTerrainSystem-driven).
    /// Host wires this to the background-JPG painter. Must be called
    /// AFTER `Engine::initialize` so the system is constructed.
    using TileDestroyCallback =
        std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setTileDestroyCallback(TileDestroyCallback cb);

private:
    GLFWwindow*              window_         = nullptr;
    UserComponentIds         ids_;
    CameraSystem*            camera_         = nullptr;   // borrowed
    BulletTerrainSystem*     bulletTerrain_  = nullptr;   // borrowed
    TerrainCollisionSystem*  collision_      = nullptr;   // borrowed
    threadmaxx::EntityHandle playerShip_     = {};
    std::filesystem::path    levelDir_;   // empty -> synthetic arena
    std::int32_t             cellsX_         = 0;
    std::int32_t             cellsY_         = 0;
};

} // namespace tou2d
