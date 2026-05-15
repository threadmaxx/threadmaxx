#include "CameraSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <cmath>

namespace rpg {

namespace {

constexpr float kPitchMin   = -1.3f;
constexpr float kPitchMax   =  1.3f;
constexpr float kMinDist    =  3.0f;
constexpr float kMaxDist    =  18.0f;

} // namespace

void CameraSystem::update(threadmaxx::SystemContext& ctx) {
    yaw_      += input().yawDelta;
    pitch_     = std::clamp(pitch_ + input().pitchDelta, kPitchMin, kPitchMax);
    distance_  = std::clamp(distance_ + input().zoomDelta, kMinDist, kMaxDist);
    // Consume the zoom delta — yawDelta/pitchDelta are refreshed each
    // poll so they don't accumulate.
    input().zoomDelta = 0.0f;

    const auto& w = ctx.world();
    const auto player = worldState_->player;
    if (!player.valid() || !w.alive(player)) return;

    const auto& tr = w.get<threadmaxx::Transform>(player);
    playerPos_ = tr.position;

    // Push the updated yaw back into the player's state so movement
    // matches the camera facing.
    auto* ids = ids_;
    const float yaw = yaw_;
    ctx.single([player, ids, yaw, &w]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        const PlayerState* ps =
            threadmaxx::user::tryGet<PlayerState>(w, ids->playerState, player);
        if (!ps) return;
        PlayerState updated = *ps;
        updated.yawRadians = yaw;
        threadmaxx::removeUserComponent(cb, ids->playerState, player);
        threadmaxx::addUserComponent(cb, ids->playerState, player, updated);
    });
}

void CameraSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    const float cy = std::cos(yaw_);
    const float sy = std::sin(yaw_);
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);

    // Orbit around the player. Camera sits behind / above by distance_.
    const threadmaxx::Vec3 target{playerPos_.x, playerPos_.y + 1.2f, playerPos_.z};
    const threadmaxx::Vec3 eye{
        target.x + sy * cp * distance_,
        target.y + sp * distance_,
        target.z + cy * cp * distance_,
    };
    const threadmaxx::Vec3 fwd{
        target.x - eye.x,
        target.y - eye.y,
        target.z - eye.z,
    };

    threadmaxx::Camera cam = {};
    cam.id        = 0;
    cam.mode      = threadmaxx::ProjectionMode::Perspective;
    cam.position  = eye;
    cam.forward   = fwd;
    cam.up        = {0, 1, 0};
    cam.nearZ     = 0.1f;
    cam.farZ      = 200.0f;
    cam.fovYRadians = 1.05f;
    cam.aspect    = static_cast<float>(worldState_->framebufferWidth) /
                    static_cast<float>(std::max(worldState_->framebufferHeight, 1u));

    // View matrix (right-handed, -Z forward in camera space).
    auto normalize = [](threadmaxx::Vec3 v) {
        const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        return n > 0.0f ? threadmaxx::Vec3{v.x / n, v.y / n, v.z / n}
                        : threadmaxx::Vec3{0, 0, 1};
    };
    auto cross = [](threadmaxx::Vec3 a, threadmaxx::Vec3 b) {
        return threadmaxx::Vec3{a.y * b.z - a.z * b.y,
                                a.z * b.x - a.x * b.z,
                                a.x * b.y - a.y * b.x};
    };
    const threadmaxx::Vec3 f = normalize(fwd);
    const threadmaxx::Vec3 s = normalize(cross(f, cam.up));
    const threadmaxx::Vec3 u = cross(s, f);
    cam.view = {
        s.x, u.x, -f.x, 0,
        s.y, u.y, -f.y, 0,
        s.z, u.z, -f.z, 0,
        -(s.x * eye.x + s.y * eye.y + s.z * eye.z),
        -(u.x * eye.x + u.y * eye.y + u.z * eye.z),
        +(f.x * eye.x + f.y * eye.y + f.z * eye.z),
        1.0f,
    };

    // Perspective matrix.
    const float fy = 1.0f / std::tan(cam.fovYRadians * 0.5f);
    const float nf = 1.0f / (cam.nearZ - cam.farZ);
    cam.projection = {
        fy / cam.aspect, 0,  0,                                  0,
        0,               fy, 0,                                  0,
        0,               0,  (cam.farZ + cam.nearZ) * nf,        -1.0f,
        0,               0,  (2.0f * cam.farZ * cam.nearZ) * nf, 0,
    };

    b.addCamera(cam);
}

} // namespace rpg
