#include "PlayerInputSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

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

    ctx.single([player, vx, vz](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        cb.setVelocity(player, threadmaxx::Velocity{{vx, 0.0f, vz}, {0, 0, 0}});
    });
}

} // namespace rpg
