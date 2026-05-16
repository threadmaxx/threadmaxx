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

    // §3.11.2 batch D2 — aim PIP visibility = sword currently swinging.
    if (const PlayerState* ps =
            threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player)) {
        drawAimPipThisFrame_ = ps->swordSwingTimer > 0.0f;
    } else {
        drawAimPipThisFrame_ = false;
    }

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

namespace {

// View matrix construction (right-handed, -Z forward in camera space).
threadmaxx::Camera buildPerspective(threadmaxx::Vec3 eye,
                                    threadmaxx::Vec3 target,
                                    threadmaxx::Vec3 worldUp,
                                    float fovY, float aspect,
                                    float nearZ, float farZ) {
    threadmaxx::Camera cam = {};
    cam.mode        = threadmaxx::ProjectionMode::Perspective;
    cam.position    = eye;
    cam.forward     = {target.x - eye.x, target.y - eye.y, target.z - eye.z};
    cam.up          = worldUp;
    cam.nearZ       = nearZ;
    cam.farZ        = farZ;
    cam.fovYRadians = fovY;
    cam.aspect      = aspect;

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
    const threadmaxx::Vec3 f = normalize(cam.forward);
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
    const float fy = 1.0f / std::tan(fovY * 0.5f);
    const float nf = 1.0f / (nearZ - farZ);
    cam.projection = {
        fy / aspect, 0,  0,                            0,
        0,           fy, 0,                            0,
        0,           0,  (farZ + nearZ) * nf,          -1.0f,
        0,           0,  (2.0f * farZ * nearZ) * nf,   0,
    };
    return cam;
}

// §3.11.2 batch D2 — top-down ortho looking straight down. View matrix
// is the same as buildPerspective except up/forward differ; projection
// is orthographic.
threadmaxx::Camera buildTopDownOrtho(threadmaxx::Vec3 center,
                                     float halfHeight, float aspect,
                                     float nearZ, float farZ) {
    threadmaxx::Camera cam = {};
    cam.mode        = threadmaxx::ProjectionMode::Orthographic;
    cam.position    = {center.x, center.y + 40.0f, center.z};
    cam.forward     = {0.0f, -1.0f, 0.0f};
    cam.up          = {0.0f, 0.0f, -1.0f};  // top of mini-map = world -Z
    cam.nearZ       = nearZ;
    cam.farZ        = farZ;
    cam.aspect      = aspect;
    cam.orthoSize   = halfHeight;
    // Hand-built view: eye looks straight down at center.
    cam.view = {
        1, 0, 0, 0,
        0, 0, -1, 0,
        0, 1, 0, 0,
        -center.x, center.z, -cam.position.y, 1.0f,
    };
    const float halfWidth = halfHeight * aspect;
    const float nf = 1.0f / (nearZ - farZ);
    cam.projection = {
        1.0f / halfWidth,  0,                  0,             0,
        0,                 1.0f / halfHeight,  0,             0,
        0,                 0,                  nf,            0,
        0,                 0,                  nearZ * nf,    1.0f,
    };
    return cam;
}

} // namespace

void CameraSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    const float cy = std::cos(yaw_);
    const float sy = std::sin(yaw_);
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);

    const threadmaxx::Vec3 target{playerPos_.x, playerPos_.y + 1.2f, playerPos_.z};
    const threadmaxx::Vec3 eye{
        target.x + sy * cp * distance_,
        target.y + sp * distance_,
        target.z + cy * cp * distance_,
    };

    const float fbW = static_cast<float>(worldState_->framebufferWidth);
    const float fbH = static_cast<float>(std::max(worldState_->framebufferHeight, 1u));

    // §3.11.2 batch D2 — emit up to three cameras per frame:
    //   [0] main third-person (full screen).
    //   [1] top-down mini-map (top-right corner).
    //   [2] over-the-shoulder aim PIP (center, only when sword is drawn).
    // Stash a copy in WorldState::activeCameras so CubeRenderSystem can
    // run frustum culling against the same set.
    worldState_->activeCameras.clear();

    // -- Main (full-screen perspective). --
    threadmaxx::Camera main = buildPerspective(
        eye, target, {0, 1, 0}, /*fovY*/ 1.05f,
        /*aspect*/ (fbW * kViewportMain.width) / (fbH * kViewportMain.height),
        /*near*/ 0.1f, /*far*/ 200.0f);
    main.id       = kCameraIdMain;
    main.viewport = kViewportMain;
    worldState_->activeCameras.push_back(main);
    b.addCamera(main);

    // -- Top-down mini-map (orthographic). --
    threadmaxx::Camera minimap = buildTopDownOrtho(
        playerPos_,
        /*halfHeight*/ 35.0f,
        /*aspect*/ (fbW * kViewportMinimap.width) / (fbH * kViewportMinimap.height),
        /*near*/ 0.5f, /*far*/ 100.0f);
    minimap.id       = kCameraIdMinimap;
    minimap.viewport = kViewportMinimap;
    worldState_->activeCameras.push_back(minimap);
    b.addCamera(minimap);

    // -- Aim PIP (narrow FOV, only while sword is drawn). --
    if (drawAimPipThisFrame_) {
        // Shoulder-mounted: forward from player, low pitch.
        const threadmaxx::Vec3 aimEye{
            target.x - sy * 1.5f,
            target.y + 0.5f,
            target.z - cy * 1.5f,
        };
        const threadmaxx::Vec3 aimTgt{
            target.x - sy * 20.0f,
            target.y,
            target.z - cy * 20.0f,
        };
        threadmaxx::Camera aim = buildPerspective(
            aimEye, aimTgt, {0, 1, 0},
            /*fovY*/ 0.5f,  // narrow
            /*aspect*/ (fbW * kViewportAimPip.width) / (fbH * kViewportAimPip.height),
            /*near*/ 0.1f, /*far*/ 50.0f);
        aim.id       = kCameraIdAim;
        aim.viewport = kViewportAimPip;
        worldState_->activeCameras.push_back(aim);
        b.addCamera(aim);
    }
}

} // namespace rpg
