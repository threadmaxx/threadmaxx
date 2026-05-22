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

    // Per-tick cached state. yaw_ / pitch_ are mirrored from
    // PlayerState each tick — the canonical source is the player's
    // user component, which PlayerInputSystem owns.
    threadmaxx::Vec3 playerPos_{0, 1, 0};
    float            yaw_       = 0.0f;
    float            pitch_     = 0.0f;
    float            distance_  = 8.0f;
    /// 2026-05-22 audit refactor — mirrored from
    /// PlayerState.firstPerson. `buildRenderFrame` consults it to
    /// choose between the eye-at-head FPS framing and the legacy
    /// orbit framing.
    bool             firstPerson_         = false;
    /// §3.11.2 batch D2 — sticky aim-PIP toggle (V key).
    /// `buildRenderFrame` checks this to decide whether to push the
    /// aim-PIP camera into the render frame.
    bool             drawAimPipThisFrame_ = false;
};

} // namespace rpg
