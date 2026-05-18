#include "CubeRenderSystem.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>
#include <threadmaxx/render/Visibility.hpp>

#include <vector>

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
        s.meshId    = cr->meshId;
        s.entity    = e;
        snapshot_.push_back(s);
    }
}

void CubeRenderSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (snapshot_.empty()) return;

    // §3.11.2 batch D2 — build a parallel (DrawItem, AABB) array and
    // let `cullByFrustum` populate `DrawItem::cameraMask` per item.
    // Each camera in `WorldState::activeCameras` claims one bit; the
    // renderer skips items whose mask doesn't include the camera's
    // current bit. With three cameras emitted today (main, mini-map,
    // aim), `cameraMask` is a 3-bit value per draw item.
    std::vector<threadmaxx::DrawItem>       items;
    std::vector<threadmaxx::BoundingVolume> bounds;
    items.reserve(snapshot_.size());
    bounds.reserve(snapshot_.size());
    for (const auto& s : snapshot_) {
        threadmaxx::DrawItem di = {};
        di.entity    = s.entity;
        di.transform = s.transform;
        di.transform.scale = {
            s.transform.scale.x * s.scale,
            s.transform.scale.y * s.scale,
            s.transform.scale.z * s.scale,
        };
        di.meshId       = s.meshId;        // §3.11 batch 9b.2b
        di.materialId   = 0;
        di.materialOverride.params = {s.color[0], s.color[1], s.color[2], s.color[3]};
        // Default = all cameras; cullByFrustum overwrites below.
        di.cameraMask   = ~0u;
        items.push_back(di);

        // Conservative AABB centered at the entity position with
        // scale-derived half-extents. The `transform.scale` already
        // includes the per-cube scale (computed above).
        threadmaxx::BoundingVolume bv;
        const float hx = 0.5f * std::abs(di.transform.scale.x);
        const float hy = 0.5f * std::abs(di.transform.scale.y);
        const float hz = 0.5f * std::abs(di.transform.scale.z);
        bv.min = {s.transform.position.x - hx, s.transform.position.y - hy,
                  s.transform.position.z - hz};
        bv.max = {s.transform.position.x + hx, s.transform.position.y + hy,
                  s.transform.position.z + hz};
        bounds.push_back(bv);
    }

    if (worldState_ && !worldState_->activeCameras.empty()) {
        threadmaxx::cullByFrustum(
            std::span<threadmaxx::DrawItem>(items),
            std::span<const threadmaxx::BoundingVolume>(bounds),
            std::span<const threadmaxx::Camera>(worldState_->activeCameras));
    }

    for (const auto& di : items) {
        if (di.cameraMask == 0) continue;   // visible to no camera
        b.addDrawItem(threadmaxx::RenderPass::Opaque, di);
    }
}

} // namespace rpg
