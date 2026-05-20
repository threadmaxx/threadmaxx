#include "PlayerInputSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <cmath>

namespace rpg {

void PlayerInputSystem::update(threadmaxx::SystemContext& ctx) {
    const auto player = worldState_->player;
    if (!player.valid()) return;

    const auto& w = ctx.world();
    if (!w.alive(player)) return;

    const PlayerState* ps =
        threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
    if (!ps) return;

    const float yaw   = ps->yawRadians;
    const float speed = ps->runSpeed;
    const float forward = input().forward;
    const float strafe  = input().strafe;

    const float cosY = std::cos(yaw);
    const float sinY = std::sin(yaw);
    // World-space velocity. yaw=0 → facing -Z (matches the camera math
    // in CameraSystem). Strafe is rightward.
    const float vx = (-sinY * forward + cosY * strafe) * speed;
    const float vz = (-cosY * forward - sinY * strafe) * speed;

    // §3.11.1 batch D1: consume the attack edge + age the swing timer.
    // The edges are global; takeEdges() atomically reads + clears, so
    // only one system per tick observes a given press.
    const std::uint32_t edges = takeEdges();
    const bool attackPressed = (edges & kEdgeAttack) != 0;
    if (edges & kEdgeAimToggle) {
        // 2026-05-20 — V toggles the over-the-shoulder PIP. We flip
        // it here because PlayerInputSystem is the one that owns the
        // edges (takeEdges clears them atomically); routing through
        // WorldState gives CameraSystem a stable per-tick read.
        worldState_->aimPipVisible = !worldState_->aimPipVisible;
    }

    PlayerState updated = *ps;
    const float dt = static_cast<float>(ctx.dt());
    if (updated.swordSwingTimer > 0.0f) {
        updated.swordSwingTimer = std::max(0.0f, updated.swordSwingTimer - dt);
    }
    if (attackPressed && updated.swordSwingTimer <= 0.0f) {
        updated.swordSwingTimer = kSwordSwingSeconds;
    }
    // §3.11 batch D-audit fix: write the player's world
    // `Transform.orientation` from the camera yaw. Pre-fix the
    // player's orientation was never updated, so the sword
    // (Parent-attached child) extended in a fixed world direction
    // regardless of which way the player was facing — combat could
    // only "hit" entities at one specific world point. With this
    // write, HierarchySystem propagates the rotation into the sword,
    // and CombatSystem's tip computation rotates with the camera.
    //
    // Yaw rotation around the world Y axis: q = (0, sin(yaw/2), 0,
    // cos(yaw/2)).
    const auto& currentT = w.get<threadmaxx::Transform>(player);
    threadmaxx::Transform newT = currentT;
    const float half = yaw * 0.5f;
    newT.orientation.x = 0.0f;
    newT.orientation.y = std::sin(half);
    newT.orientation.z = 0.0f;
    newT.orientation.w = std::cos(half);

    // 2026-05-20 — visible sword-swing animation. Without this the
    // F-press flipped a hidden hit-window flag and the sword cube
    // never moved; the user reported "sword does not move". We now
    // arc the sword's local-offset position+orientation around the
    // player-local Y axis from +1.1 rad → -1.1 rad across the swing
    // window. The same setParent re-issue runs once on the trailing
    // edge so the resting pose snaps back. The sword's resting
    // local offset matches the values seeded in DemoGame::onSetup
    // (`{0.5, 0.8, -0.8}` position, `{0.18, 0.18, 1.4}` scale).
    const auto sword = worldState_->sword;
    const float currSwing = updated.swordSwingTimer;
    const bool  swingActive = currSwing > 0.0f;
    const bool  swingJustEnded =
        prevSwingTimer_ > 0.0f && currSwing <= 0.0f;
    prevSwingTimer_ = currSwing;

    threadmaxx::Parent swordParent = {};
    bool writeSwordParent = false;
    if (sword.valid() && w.alive(sword)) {
        swordParent.parent = player;
        swordParent.localOffset.position = {0.5f, 0.8f, -0.8f};
        swordParent.localOffset.scale    = {0.18f, 0.18f, 1.4f};
        if (swingActive) {
            // 2026-05-20 — overhead-to-forward chop around the +X
            // axis. Previous attempt rotated around +Y and ALSO
            // orbited the pivot, which dragged the sword across to
            // the LEFT side of the player even though it's held on
            // the right; user reported "swings on the opposite side
            // from where the player holds it". A pure X-rotation
            // keeps the grip anchored at (+0.5, 0.8, -0.8) and only
            // sweeps the blade in the player-local YZ plane: starts
            // raised overhead (a=+1.0), passes forward (a=0) at
            // mid-swing, ends at a slight downward thrust (a=-0.2).
            const float progress =
                1.0f - currSwing / kSwordSwingSeconds;          // 0 → 1
            const float a = kSwingAngleStart +
                            progress * (kSwingAngleEnd - kSwingAngleStart);
            // Quaternion for rotation around local +X by `a`.
            const float half = a * 0.5f;
            swordParent.localOffset.orientation = {
                std::sin(half), 0.0f, 0.0f, std::cos(half),
            };
            writeSwordParent = true;
        } else if (swingJustEnded) {
            swordParent.localOffset.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
            writeSwordParent = true;
        }
    }

    const auto idsPS = ids_->playerState;
    ctx.single([player, vx, vz, updated, idsPS, newT,
                sword, swordParent, writeSwordParent]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        cb.setVelocity(player, threadmaxx::Velocity{{vx, 0.0f, vz}, {0, 0, 0}});
        cb.setTransform(player, newT);
        threadmaxx::addUserComponent(cb, idsPS, player, updated);
        if (writeSwordParent) cb.setParent(sword, swordParent);
    });
}

} // namespace rpg
