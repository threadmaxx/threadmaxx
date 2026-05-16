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

    const auto idsPS = ids_->playerState;
    ctx.single([player, vx, vz, updated, idsPS, newT]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        cb.setVelocity(player, threadmaxx::Velocity{{vx, 0.0f, vz}, {0, 0, 0}});
        cb.setTransform(player, newT);
        threadmaxx::addUserComponent(cb, idsPS, player, updated);
    });
}

} // namespace rpg
