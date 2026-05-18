/// @file SkinnedRenderSystem.cpp
/// §3.11.7b.5 batch 9b.4.c — emits a single skinned DrawItem.

#include "SkinnedRenderSystem.hpp"

#include <threadmaxx/render/RenderFrameBuilder.hpp>

namespace rpg {

void SkinnedRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!worldState_ || worldState_->skinnedMeshId <= 0) {
        // Renderer's `registerSkinnedMeshFromData` wasn't wired (e.g.
        // headless test). Skip — falling silent is the right move
        // since there's no skinned mesh to draw.
        return;
    }

    threadmaxx::DrawItem di = {};
    di.entity = threadmaxx::kInvalidEntity;
    di.transform.position = threadmaxx::Vec3{8.0f, 0.0f, 5.0f};
    di.transform.orientation = threadmaxx::Quat{0, 0, 0, 1};
    di.transform.scale = threadmaxx::Vec3{1, 1, 1};
    di.meshId    = worldState_->skinnedMeshId;
    di.materialId = 0;
    // `skeletonId >= 0` is the renderer's "use the skinned pipeline"
    // dispatch signal (§9b.4.b). The id value itself is not consulted
    // here — only its sign — but a future multi-skeleton scene would
    // route the bone-buffer offset by skeletonId. We use
    // `pose.ringSlot` as the immediate bone-base offset (0 for our
    // single skinned entity).
    di.skeletonId = 0;
    di.pose.ringSlot = 0;
    di.materialOverride.params = {0.4f, 0.85f, 0.45f, 1.0f};
    di.cameraMask = ~0u;

    b.addDrawItem(threadmaxx::RenderPass::Opaque, di);
}

} // namespace rpg
