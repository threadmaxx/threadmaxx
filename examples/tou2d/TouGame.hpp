#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

#include <filesystem>

struct GLFWwindow;

namespace tou2d {

class CameraSystem;

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

private:
    GLFWwindow*              window_     = nullptr;
    UserComponentIds         ids_;
    CameraSystem*            camera_     = nullptr;   // borrowed; owned by Engine
    threadmaxx::EntityHandle playerShip_ = {};
    std::filesystem::path    levelDir_;   // empty -> synthetic arena
};

} // namespace tou2d
