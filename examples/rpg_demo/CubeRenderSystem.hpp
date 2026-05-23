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
    // 2026-05-20 — parallel arrays for the SIMD sphere broad-phase.
    // `centers_[i]` is the AABB center for `items_[i]`, `radii_[i]`
    // is the bounding sphere radius. Built in `update` and consumed
    // by `simd::frustum_cull` in `buildRenderFrame`. Kept as member
    // state so steady-state ticks reuse the allocation.
    std::vector<threadmaxx::Vec3>           centers_;
    std::vector<float>                      radii_;
    // §3.11 batch D12 audit — sphere-visibility scratch. Reused
    // across ticks so the BRF cull doesn't allocate per call (was
    // a fresh `std::vector<uint8_t>(n * cams)` every tick).
    std::vector<std::uint8_t>               sphereVisible_;
};

} // namespace rpg
