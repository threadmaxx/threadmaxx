#pragma once

#include <threadmaxx/System.hpp>
#include <threadmaxx/render/DrawItem.hpp>
#include <threadmaxx/render/Visibility.hpp>

#include "DemoTypes.hpp"

namespace rpg {

/// Snapshots every CubeRender-bearing entity into a private buffer
/// during `update` (parallel-safe read), then re-emits them as
/// `DrawItem`s in `buildRenderFrame`. The renderer falls back to its
/// unit-cube mesh for any DrawItem without a real mesh asset.
///
/// 2026-05-20 — DrawItem + AABB construction moved into update so
/// the heavy per-entity work runs across worker jobs.
/// buildRenderFrame then only runs `cullByFrustum` and emits the
/// pre-built items.
class CubeRenderSystem : public threadmaxx::ISystem {
public:
    CubeRenderSystem(UserComponentIds* ids, const WorldState* worldState)
        : ids_(ids), worldState_(worldState) {}

    const char* name() const noexcept override { return "cube-render"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update(threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

private:
    UserComponentIds*                       ids_        = nullptr;
    const WorldState*                       worldState_ = nullptr;
    std::vector<threadmaxx::DrawItem>       items_;
    std::vector<threadmaxx::BoundingVolume> bounds_;
};

} // namespace rpg
