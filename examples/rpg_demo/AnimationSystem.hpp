// §3.11.6 batch D6 — procedural animation for moving entities.
//
// Reads `Transform + Velocity + AnimState` chunks; computes a Y-bob
// proportional to the entity's XZ speed and writes the modulated
// position back via the command buffer. Cheap to compute, parallel-
// safe through `forEachChunk`, and visually convincing for a cube
// world — the NPCs look like they're walking when they move and
// stand still when stationary.
//
// Runs AFTER `MovementSystem` (which integrates X/Z from Velocity)
// and BEFORE `HierarchySystem` (so Parent-attached children — the
// player's sword in particular — inherit the bobbed Y).
//
// Full skinned-mesh playback (real bone matrices + GPU pose upload
// via the engine's `AnimationStateRef` / `AnimationPoseRef` slots +
// `UploadRing`) is deferred to a renderer-side batch — see
// FUTURE_WORK §3.11.6 deferred notes.

#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

class AnimationSystem : public threadmaxx::ISystem {
public:
    AnimationSystem(UserComponentIds* ids, const WorldState* worldState = nullptr)
        : ids_(ids), worldState_(worldState) {}

    const char* name() const noexcept override { return "animation"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform}
             | threadmaxx::ComponentSet{threadmaxx::Component::Velocity};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }

    void update(threadmaxx::SystemContext& ctx) override;

private:
    UserComponentIds*  ids_        = nullptr;
    const WorldState*  worldState_ = nullptr;
};

} // namespace rpg
