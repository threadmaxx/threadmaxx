#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

/// Third-person follow camera. Tracks the player handle stored in
/// @ref WorldState. Reads input (yaw/pitch/zoom deltas), caches the
/// player transform during `update`, then emits a `Camera` from
/// `buildRenderFrame`.
///
/// Also writes the post-input yaw back into the player's
/// `PlayerState.yawRadians` so the input system's forward vector
/// matches the camera direction.
class CameraSystem : public threadmaxx::ISystem {
public:
    CameraSystem(WorldState* worldState, UserComponentIds* ids)
        : worldState_(worldState), ids_(ids) {}

    const char* name() const noexcept override { return "camera"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        // Updates the player's UserComponent<PlayerState>; declare
        // `all()` to keep it serial against systems that read it.
        return threadmaxx::ComponentSet::all();
    }

    void update(threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

private:
    WorldState*       worldState_ = nullptr;
    UserComponentIds* ids_        = nullptr;

    // Per-tick cached state.
    threadmaxx::Vec3 playerPos_{0, 1, 0};
    float            yaw_       = 0.0f;
    float            pitch_     = 0.45f;   // look slightly down
    float            distance_  = 8.0f;
};

} // namespace rpg
