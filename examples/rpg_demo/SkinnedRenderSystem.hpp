// §3.11.7b.5 batch 9b.4.c — single hardcoded skinned-mesh entity.
//
// Minimal "skinning demo" wiring: every tick, this system pushes
// ONE DrawItem at world position (8, 0, 5) with `skeletonId = 0`
// and `pose.ringSlot = 0` so the renderer routes it through the
// opaque-skinned pipeline. The renderer's per-frame bone-matrix
// upload (pushed by main.cpp from the SkinningSystem's output)
// supplies the actual bone math; this system just declares "draw
// the skinned capsule here."
//
// For a fuller integration (multiple skinned entities, per-entity
// pose state, glTF-loaded assets), a richer ECS scheme would
// promote the meshId + boneBase to a `SkinnedRender` user
// component. Out of scope for v1.1 — see FUTURE_WORK §3.11.7b.5
// "v1.x glTF candidate".

#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

class SkinnedRenderSystem : public threadmaxx::ISystem {
public:
    explicit SkinnedRenderSystem(const WorldState* worldState)
        : worldState_(worldState) {}

    const char* name() const noexcept override { return "skinned-render"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update(threadmaxx::SystemContext&) override {}
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

private:
    const WorldState* worldState_ = nullptr;
};

} // namespace rpg
