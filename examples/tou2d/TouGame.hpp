#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Game.hpp>

struct GLFWwindow;

namespace tou2d {

class CameraSystem;

/// IGame implementation for the M1 thrust-loop proof.
///
/// onSetup:
///   * Registers user components.
///   * Registers InputSystem (preStep), MovementSystem, CameraSystem.
///   * Spawns one ship at world origin with LocalPlayer slot=0,
///     Transform + Velocity + (initially zero) PlayerInput + a meshId
///     that the renderer fills with its default unit cube.
class TouGame : public threadmaxx::IGame {
public:
    explicit TouGame(GLFWwindow* window) noexcept;

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&  world,
                 threadmaxx::CommandBuffer& seed) override;
    void onTeardown(threadmaxx::Engine& engine,
                    threadmaxx::World&  world) override;

    /// Borrowed; set after Engine::initialize so the host can forward
    /// resize events to the camera. CameraSystem owns the viewport.
    CameraSystem* cameraSystem() noexcept { return camera_; }

private:
    GLFWwindow*         window_   = nullptr;
    UserComponentIds    ids_;
    CameraSystem*       camera_   = nullptr;  // borrowed; owned by Engine
};

} // namespace tou2d
