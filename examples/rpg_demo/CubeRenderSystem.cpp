#include "CubeRenderSystem.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

namespace rpg {

void CubeRenderSystem::update(threadmaxx::SystemContext& ctx) {
    snapshot_.clear();

    const auto& w = ctx.world();
    const auto entities = w.entities();
    const auto masks = w.componentMasks();
    const auto transforms = w.transforms();

    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (masks[i].has(threadmaxx::Component::DisabledTag)) continue;
        const auto e = entities[i];
        const CubeRender* cr =
            threadmaxx::user::tryGet<CubeRender>(w, ids_->cubeRender, e);
        if (!cr) continue;
        Snapshot s;
        s.transform = transforms[i];
        s.color[0]  = cr->color[0];
        s.color[1]  = cr->color[1];
        s.color[2]  = cr->color[2];
        s.color[3]  = cr->color[3];
        s.scale     = cr->scale;
        s.entity    = e;
        snapshot_.push_back(s);
    }
}

void CubeRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    for (const auto& s : snapshot_) {
        threadmaxx::DrawItem di = {};
        di.entity    = s.entity;
        di.transform = s.transform;
        // Bake the per-cube scale into the transform so the renderer's
        // shader doesn't need a separate path.
        di.transform.scale = {
            s.transform.scale.x * s.scale,
            s.transform.scale.y * s.scale,
            s.transform.scale.z * s.scale,
        };
        di.meshId       = 0;
        di.materialId   = 0;
        di.materialOverride.params = {s.color[0], s.color[1], s.color[2], s.color[3]};
        di.cameraMask   = ~0u;
        b.addDrawItem(threadmaxx::RenderPass::Opaque, di);
    }
}

} // namespace rpg
