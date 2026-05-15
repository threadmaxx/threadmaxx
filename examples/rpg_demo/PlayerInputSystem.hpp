#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

/// Reads the per-tick `InputState` (filled by GLFW) + the camera yaw
/// stored in @ref WorldState, and writes a `Velocity` to the player
/// entity. The player's `PlayerState.yawRadians` is updated by the
/// camera system; this system reads it without locking because both
/// land in different waves (CameraSystem writes nothing).
class PlayerInputSystem : public threadmaxx::ISystem {
public:
    PlayerInputSystem(WorldState* worldState, UserComponentIds* ids)
        : worldState_(worldState), ids_(ids) {}

    const char* name() const noexcept override { return "player-input"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Velocity};
    }
    void update(threadmaxx::SystemContext& ctx) override;

private:
    WorldState*       worldState_ = nullptr;
    UserComponentIds* ids_        = nullptr;
};

} // namespace rpg
