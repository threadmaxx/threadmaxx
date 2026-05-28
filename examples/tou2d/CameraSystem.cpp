#include "CameraSystem.hpp"

#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/Light.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

namespace tou2d {

namespace {

/// Build a column-major right-handed orthographic projection matrix
/// targeting Vulkan NDC z ∈ [0, 1] (CLAUDE.md "Projection MUST be
/// Vulkan-style"). With camera looking down -Z, a point at eye-space
/// z' = -n maps to NDC z=0, z' = -f maps to NDC z=1. Mirrors the
/// `buildTopDownOrtho` form in `examples/rpg_demo/CameraSystem.cpp`.
std::array<float, 16> buildOrthoProj(float halfW, float halfH, float n, float f) noexcept {
    const float invHW = 1.0f / halfW;
    const float invHH = 1.0f / halfH;
    const float nf    = 1.0f / (n - f);
    return {
        invHW,  0.0f,   0.0f,     0.0f,
        0.0f,   invHH,  0.0f,     0.0f,
        0.0f,   0.0f,   nf,       0.0f,
        0.0f,   0.0f,   n * nf,   1.0f,
    };
}

/// Build a view matrix for an eye at (cx, cy, eyeZ) looking down -Z,
/// up = +Y. Column-major.
std::array<float, 16> buildView2D(float cx, float cy, float eyeZ) noexcept {
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -cx,  -cy,  -eyeZ, 1.0f,
    };
}

} // namespace

CameraSystem::CameraSystem(UserComponentIds ids) noexcept : ids_(ids) {}

void CameraSystem::setViewport(std::uint32_t width, std::uint32_t height) noexcept {
    viewportW_ = width  > 0 ? width  : 1;
    viewportH_ = height > 0 ? height : 1;
}

threadmaxx::Viewport CameraSystem::viewportFor(std::uint8_t humanSlot) const noexcept {
    if (humanSlot >= numHumans_) return threadmaxx::Viewport{0.0f, 0.0f, 0.0f, 0.0f};
    switch (numHumans_) {
    case 1: {
        return threadmaxx::Viewport{0.0f, 0.0f, 1.0f, 1.0f};
    }
    case 2: {
        // Slot 0 left half, slot 1 right half.
        const float x = humanSlot == 0 ? 0.0f : 0.5f;
        return threadmaxx::Viewport{x, 0.0f, 0.5f, 1.0f};
    }
    case 3:
    case 4: {
        // 2x2 grid. Slot 0 TL, 1 TR, 2 BL, 3 BR. Y is "from the top"
        // per the renderer's Viewport convention.
        const float x = (humanSlot & 1u) ? 0.5f : 0.0f;
        const float y = (humanSlot & 2u) ? 0.5f : 0.0f;
        return threadmaxx::Viewport{x, y, 0.5f, 0.5f};
    }
    default:
        return threadmaxx::Viewport{0.0f, 0.0f, 1.0f, 1.0f};
    }
}

float CameraSystem::viewportAspect() const noexcept {
    const threadmaxx::Viewport vp = viewportFor(0);
    const float subPixelW =
        vp.width  * static_cast<float>(viewportW_);
    const float subPixelH =
        vp.height * static_cast<float>(viewportH_ > 0 ? viewportH_ : 1);
    return subPixelW / (subPixelH > 0.0f ? subPixelH : 1.0f);
}

void CameraSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsLp = ids_.localPlayer;
    if (!idsLp.valid()) return;

    // M5.1 — latch every human slot's position so buildRenderFrame can
    // emit one camera per human. Bots are skipped (isBot==1). A human
    // slot whose ship is missing keeps the previous tick's value
    // (camera holds still rather than snapping to origin).
    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& /*cb*/) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(idsLp.componentBit()))               continue;

            const auto lpSpan = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto& transforms = chunk.transforms;
            for (std::size_t row = 0; row < lpSpan.size(); ++row) {
                const LocalPlayer& lp = lpSpan[row];
                if (lp.isBot != 0) continue;
                if (lp.slot >= followTargets_.size()) continue;
                followTargets_[lp.slot] = transforms[row].position;
            }
        }
    });
}

void CameraSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    const float aspect = viewportAspect();
    const float halfH  = orthoHalfH_;
    const float halfW  = halfH * aspect;

    for (std::uint8_t slot = 0; slot < numHumans_; ++slot) {
        const threadmaxx::Vec3 center = followTargets_[slot];

        threadmaxx::Camera cam = {};
        cam.id        = static_cast<std::uint32_t>(slot);
        cam.mode      = threadmaxx::ProjectionMode::Orthographic;
        cam.position  = {center.x, center.y, 50.0f};
        cam.forward   = {0.0f, 0.0f, -1.0f};
        cam.up        = {0.0f, 1.0f, 0.0f};
        cam.nearZ     = 0.1f;
        cam.farZ      = 200.0f;
        cam.aspect    = aspect;
        cam.orthoSize = orthoHalfH_;
        cam.viewport  = viewportFor(slot);
        cam.view       = buildView2D(center.x, center.y, cam.position.z);
        cam.projection = buildOrthoProj(halfW, halfH, cam.nearZ, cam.farZ);

        b.addCamera(cam);
    }

    // A weak directional light from "above" gives the rendered cubes a
    // tiny gradient — useful while the cube fallback path is still
    // active. The sprite layer is flat-shaded and ignores lights.
    threadmaxx::Light l = {};
    l.type      = threadmaxx::LightType::Directional;
    l.direction = {0.0f, -1.0f, -0.3f};
    l.color     = {1.0f, 0.95f, 0.9f};
    l.intensity = 1.0f;
    b.addLight(l);
}

} // namespace tou2d
