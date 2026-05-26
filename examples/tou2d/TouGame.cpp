#include "TouGame.hpp"

#include "CameraSystem.hpp"
#include "InputSystem.hpp"
#include "MovementSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>

namespace tou2d {

TouGame::TouGame(GLFWwindow* window) noexcept : window_(window) {}

void TouGame::onSetup(threadmaxx::Engine& engine,
                      threadmaxx::World&  /*world*/,
                      threadmaxx::CommandBuffer& seed) {
    // ---- User components ----------------------------------------------------
    ids_.playerInput = engine.registerUserComponent<PlayerInput>();
    ids_.localPlayer = engine.registerUserComponent<LocalPlayer>();
    ids_.ship        = engine.registerUserComponent<Ship>();

    // ---- Systems ------------------------------------------------------------
    auto input    = std::make_unique<InputSystem>(window_, ids_);
    auto movement = std::make_unique<MovementSystem>(ids_);
    auto camera   = std::make_unique<CameraSystem>(ids_);
    camera_ = camera.get();

    engine.registerSystem(std::move(input));
    engine.registerSystem(std::move(movement));
    engine.registerSystem(std::move(camera));

    // ---- Seed the first ship ------------------------------------------------
    const auto shipH = engine.reserveEntityHandle();

    threadmaxx::Bundle b = {};
    b.transform.position = {0.0f, 0.0f, 0.0f};
    b.transform.scale    = {28.0f, 28.0f, 28.0f};   // ~32-unit ship-square
    b.velocity           = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    b.renderTag          = threadmaxx::RenderTag{0, -1, 0u};  // meshId 0 = default cube
    b.initialMask        = threadmaxx::ComponentSet{
        threadmaxx::Component::Transform,
        threadmaxx::Component::Velocity,
        threadmaxx::Component::RenderTag,
    };
    seed.spawnBundle(shipH, b);

    threadmaxx::addUserComponent(seed, ids_.localPlayer, shipH, LocalPlayer{/*slot*/ 0, {}});
    threadmaxx::addUserComponent(seed, ids_.playerInput, shipH, PlayerInput{});
    threadmaxx::addUserComponent(seed, ids_.ship, shipH, Ship{});
}

void TouGame::onTeardown(threadmaxx::Engine& /*engine*/,
                         threadmaxx::World&  /*world*/) {
    camera_ = nullptr;
}

} // namespace tou2d
