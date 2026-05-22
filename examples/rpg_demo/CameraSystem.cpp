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

constexpr float kMinDist    =  3.0f;
constexpr float kMaxDist    =  18.0f;

} // namespace

void CameraSystem::update(threadmaxx::SystemContext& ctx) {
    // 2026-05-22 audit refactor — CameraSystem no longer owns the
    // yaw / pitch accumulators. PlayerInputSystem applies the
    // mouse-look + Q/E deltas to `PlayerState.{yawRadians,
    // pitchRadians}` directly; CameraSystem just reads them.
    // Pre-refactor CameraSystem ALSO wrote `PlayerState.yawRadians`
    // back via `removeUserComponent + addUserComponent`, which
    // forced a chunk migration every tick — gone now.
    //
    // Distance is still a CameraSystem-local field because zoom is
    // a camera property, not a player intent. Scroll-wheel events
    // arrive as additive deltas via input().zoomDelta; we consume
    // them once per tick.
    distance_  = std::clamp(distance_ + input().zoomDelta, kMinDist, kMaxDist);
    input().zoomDelta = 0.0f;

    const auto& w = ctx.world();
    const auto player = worldState_->player;
    if (!player.valid() || !w.alive(player)) return;

    const PlayerState* ps =
        threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
    if (!ps) return;

    const auto& tr = w.get<threadmaxx::Transform>(player);
    playerPos_  = tr.position;
    yaw_        = ps->yawRadians;
    pitch_      = ps->pitchRadians;
    firstPerson_ = ps->firstPerson != 0u;

    // 2026-05-20 — aim PIP visibility is a sticky toggle on V,
    // flipped by PlayerInputSystem into WorldState::aimPipVisible.
    drawAimPipThisFrame_ = worldState_ && worldState_->aimPipVisible;
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

// §3.11.2 batch D2 — top-down ortho looking straight down.
threadmaxx::Camera buildTopDownOrtho(threadmaxx::Vec3 center,
                                     float halfHeight, float aspect,
                                     float nearZ, float farZ) {
    threadmaxx::Camera cam = {};
    cam.mode        = threadmaxx::ProjectionMode::Orthographic;
    cam.position    = {center.x, center.y + 40.0f, center.z};
    cam.forward     = {0.0f, -1.0f, 0.0f};
    cam.up          = {0.0f, 0.0f, -1.0f};
    cam.nearZ       = nearZ;
    cam.farZ        = farZ;
    cam.aspect      = aspect;
    cam.orthoSize   = halfHeight;
    cam.view = {
        1, 0, 0, 0,
        0, 0, 1, 0,
        0, -1, 0, 0,
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

    const float fbW = static_cast<float>(worldState_->framebufferWidth);
    const float fbH = static_cast<float>(std::max(worldState_->framebufferHeight, 1u));

    // §3.11.2 batch D2 — emit up to three cameras per frame:
    //   [0] main camera (full screen) — first OR third-person.
    //   [1] top-down mini-map (top-right corner; non-stress only).
    //   [2] aim PIP (when sword is drawn).
    worldState_->activeCameras.clear();

    // ---- Main camera ----
    //
    // 2026-05-22 audit refactor — first/third-person split.
    //
    // First-person: eye placed at the player's head. Round-5 raises
    //   the offset from 0.4 m to 0.75 m above the entity center: the
    //   player cube is 1.8 m tall (half-height 0.9), so eye now sits
    //   0.15 m below the top of the player ≈ forehead height. The
    //   0.4 m offset put the eye at chest level which the user
    //   described as "looking through the belly."
    //
    // Third-person: legacy orbit. Eye behind the player at
    //   `distance_`, looking at a point slightly above the
    //   player's center.
    //
    // Both modes use the same yaw / pitch source (PlayerState).
    //
    // 2026-05-22 audit (round 5) — FPV mouse-Y was inverted relative
    // to TPV. Both modes share the same `pitch_` value (mouse-up
    // decreases pitch); in TPV the eye sits at `target.y + sp*d`, so
    // a negative pitch lowers the eye and the resulting forward
    // vector points *up* toward the player → mouse-up → look-up ✓.
    // In FPV the previous form computed `fwd.y = sp` directly, which
    // pointed DOWN for negative pitch → mouse-up → look-down ✗. The
    // fix is `fwd.y = -sp`, matching the TPV sign convention.
    constexpr float kEyeHeightOffset = 0.75f;  // ≈ forehead on a 1.8m player
    threadmaxx::Vec3 eye, target;
    if (firstPerson_) {
        eye = {
            playerPos_.x,
            playerPos_.y + kEyeHeightOffset,
            playerPos_.z,
        };
        // Forward at yaw=0, pitch=0 → (0,0,-1). Negative `sp`
        // (mouse-up) → positive `-sp` → forward.y > 0 → look up.
        const threadmaxx::Vec3 fwd{
            -sy * cp,
            -sp,
            -cy * cp,
        };
        target = {eye.x + fwd.x, eye.y + fwd.y, eye.z + fwd.z};
    } else {
        target = {playerPos_.x, playerPos_.y + 1.2f, playerPos_.z};
        eye = {
            target.x + sy * cp * distance_,
            target.y + sp * distance_,
            target.z + cy * cp * distance_,
        };
    }

    threadmaxx::Camera main = buildPerspective(
        eye, target, {0, 1, 0}, /*fovY*/ firstPerson_ ? 1.20f : 1.05f,
        /*aspect*/ (fbW * kViewportMain.width) / (fbH * kViewportMain.height),
        /*near*/ firstPerson_ ? 0.05f : 0.1f, /*far*/ 200.0f);
    main.id       = kCameraIdMain;
    main.viewport = kViewportMain;
    worldState_->activeCameras.push_back(main);
    b.addCamera(main);

    // ---- Top-down mini-map (orthographic). ----
    if (!worldState_->stressMode) {
        threadmaxx::Camera minimap = buildTopDownOrtho(
            playerPos_,
            /*halfHeight*/ 35.0f,
            /*aspect*/ (fbW * kViewportMinimap.width) / (fbH * kViewportMinimap.height),
            /*near*/ 0.5f, /*far*/ 100.0f);
        minimap.id       = kCameraIdMinimap;
        minimap.viewport = kViewportMinimap;
        worldState_->activeCameras.push_back(minimap);
        b.addCamera(minimap);
    }

    // ---- Aim PIP (narrow FOV, only while sticky-toggled on). ----
    if (drawAimPipThisFrame_) {
        const threadmaxx::Vec3 pivot{
            playerPos_.x, playerPos_.y + 1.2f, playerPos_.z};
        const threadmaxx::Vec3 aimEye{
            pivot.x + sy * 2.5f - cy * 0.8f,
            pivot.y + 1.4f,
            pivot.z + cy * 2.5f + sy * 0.8f,
        };
        const threadmaxx::Vec3 aimTgt{
            pivot.x - sy * 6.0f,
            pivot.y,
            pivot.z - cy * 6.0f,
        };
        threadmaxx::Camera aim = buildPerspective(
            aimEye, aimTgt, {0, 1, 0},
            /*fovY*/ 0.6f,
            /*aspect*/ (fbW * kViewportAimPip.width) / (fbH * kViewportAimPip.height),
            /*near*/ 0.1f, /*far*/ 50.0f);
        aim.id       = kCameraIdAim;
        aim.viewport = kViewportAimPip;
        worldState_->activeCameras.push_back(aim);
        b.addCamera(aim);
    }
}

} // namespace rpg
