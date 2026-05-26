#include "CameraSystem.hpp"

#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/Camera.hpp>
#include <threadmaxx/render/Light.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

namespace tou2d {

namespace {

/// Build a column-major right-handed orthographic projection matrix in
/// GL-style NDC z ∈ [-1, 1]. The Vulkan renderer in the project flips
/// Y at the viewport stage and treats GL-form perspective matrices as
/// input — same convention applies for ortho.
std::array<float, 16> buildOrthoProj(float halfW, float halfH, float n, float f) noexcept {
    const float invHW = 1.0f / halfW;
    const float invHH = 1.0f / halfH;
    const float invNF = 1.0f / (n - f);
    // Column-major std::array<float, 16> — index = col*4 + row.
    return {
        invHW,  0.0f,   0.0f,            0.0f,
        0.0f,   invHH,  0.0f,            0.0f,
        0.0f,   0.0f,   2.0f * invNF,    0.0f,
        0.0f,   0.0f,   (n + f) * invNF, 1.0f,
    };
}

/// Build a view matrix for an eye at (cx, cy, eyeZ) looking down -Z,
/// up = +Y. Column-major.
std::array<float, 16> buildView2D(float cx, float cy, float eyeZ) noexcept {
    // side = (1,0,0), up = (0,1,0), forward = (0,0,-1)
    // view = (s.x, u.x, -f.x, 0; s.y, u.y, -f.y, 0; s.z, u.z, -f.z, 0; -s·e, -u·e, +f·e, 1)
    //      = (1, 0, 0, 0; 0, 1, 0, 0; 0, 0, 1, 0; -cx, -cy, -eyeZ, 1)
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

void CameraSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsLp = ids_.localPlayer;
    if (!idsLp.valid()) return;

    // Latch P1's current position so buildRenderFrame can emit a camera
    // centered on the ship. Single-job since M1 has one local player.
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
                if (lpSpan[row].slot == 0) {
                    followTarget_ = transforms[row].position;
                    return;
                }
            }
        }
    });
}

void CameraSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    threadmaxx::Camera cam = {};
    cam.id        = 0;
    cam.mode      = threadmaxx::ProjectionMode::Orthographic;
    cam.position  = {followTarget_.x, followTarget_.y, 50.0f};
    cam.forward   = {0.0f, 0.0f, -1.0f};
    cam.up        = {0.0f, 1.0f, 0.0f};
    cam.nearZ     = 0.1f;
    cam.farZ      = 200.0f;
    cam.aspect    = static_cast<float>(viewportW_) / static_cast<float>(viewportH_);
    cam.orthoSize = orthoHalfH_;

    const float halfH = orthoHalfH_;
    const float halfW = halfH * cam.aspect;
    cam.view       = buildView2D(followTarget_.x, followTarget_.y, cam.position.z);
    cam.projection = buildOrthoProj(halfW, halfH, cam.nearZ, cam.farZ);

    b.addCamera(cam);

    // A weak directional light from "above" gives the rendered cubes a
    // tiny gradient — useful while the renderer is still 3D-pipeline-
    // based. The real 2D pipeline (M2) replaces this with flat sprite
    // sampling.
    threadmaxx::Light l = {};
    l.type      = threadmaxx::LightType::Directional;
    l.direction = {0.0f, -1.0f, -0.3f};
    l.color     = {1.0f, 0.95f, 0.9f};
    l.intensity = 1.0f;
    b.addLight(l);
}

} // namespace tou2d
