#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

struct GLFWwindow;

namespace tou2d {

/// preStep system that polls the borrowed GLFW window's key state on
/// the sim thread and writes the resulting PlayerInput value to every
/// LocalPlayer-tagged entity. M1 only wires P1 (arrows + RShift +
/// RCtrl + /); P2-P4 land in M3 when local multiplayer ships.
///
/// Engine contract:
///   * Lifetime: borrows the GLFWwindow*; the caller (main.cpp) owns
///     it and is responsible for keeping it alive until engine
///     shutdown.
///   * reads()  = none — InputSystem doesn't read live world state.
///   * writes() = UserData — placeholder so the wave scheduler keeps
///                it in a separate wave from MovementSystem. The
///                actual write target is a user component (PlayerInput);
///                the engine has no scheduling category for user
///                components, so we borrow UserData's bit as the
///                conservative declaration.
///   * Driven from preStep, where commits flush immediately on the
///     sim thread before the first wave.
class InputSystem : public threadmaxx::ISystem {
public:
    InputSystem(GLFWwindow* window, UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.input"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet{threadmaxx::Component::UserData}; }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& /*ctx*/) override {}

private:
    GLFWwindow*       window_ = nullptr;
    UserComponentIds  ids_;
};

} // namespace tou2d
