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
    const auto idsPS = ids_->playerState;
    ctx.single([player, vx, vz, updated, idsPS]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        cb.setVelocity(player, threadmaxx::Velocity{{vx, 0.0f, vz}, {0, 0, 0}});
        threadmaxx::addUserComponent(cb, idsPS, player, updated);
    });
}

} // namespace rpg
