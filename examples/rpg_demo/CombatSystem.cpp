#include "CombatSystem.hpp"

#include "NPCBrainSystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

namespace rpg {

void CombatSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    const auto player = worldState_->player;
    const auto sword  = worldState_->sword;
    if (!player.valid() || !sword.valid()) return;
    if (!w.alive(player) || !w.alive(sword)) return;

    const PlayerState* ps =
        threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
    if (!ps) return;

    // §3.11.1 batch D1 — rising-edge detection: emit damage on the
    // tick that the swing actually starts. Prevents one swing from
    // damaging the same NPC every tick of its 0.30s window.
    const float currT = ps->swordSwingTimer;
    const bool  rising = (prevSwingTimer_ <= 0.0f && currT > 0.0f);
    prevSwingTimer_ = currT;
    if (!rising) return;

    const threadmaxx::Transform* swordT = w.tryGetTransform(sword);
    if (!swordT) return;

    // §3.11.1 batch D1 — sword tip is `length` along the sword's local
    // +Z axis transformed into world space. Tracking the world
    // transform's orientation is enough; we don't need the precise
    // local axis since the hierarchy already gave us a world-aligned
    // pose. Sword length is parameter-sourced via the SwordTag user
    // component.
    const SwordTag* tag =
        threadmaxx::user::tryGet<SwordTag>(w, ids_->swordTag, sword);
    const float length = tag ? tag->length : 1.4f;

    // §3.11 batch D-audit fix: sword tip extends in the player's
    // local FORWARD direction, which in this demo's convention
    // (camera math, see CameraSystem) is the -Z axis. The earlier
    // (0, 0, +length) version put the tip BEHIND the player —
    // combat was unreachable. We rotate (0, 0, -length) by the
    // sword's world orientation; with identity orientation (the
    // player's default), tip = pivot + (0, 0, -length), squarely
    // in front of the player.
    const auto& q = swordT->orientation;
    const float dirX = 0.0f, dirY = 0.0f, dirZ = -length;
    const float vx = 2.0f * (q.x * q.z + q.w * q.y) * dirZ
                   + (1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * dirX
                   + 2.0f * (q.x * q.y - q.w * q.z) * dirY;
    const float vy = 2.0f * (q.y * q.z - q.w * q.x) * dirZ
                   + 2.0f * (q.x * q.y + q.w * q.z) * dirX
                   + (1.0f - 2.0f * (q.x * q.x + q.z * q.z)) * dirY;
    const float vz = (1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * dirZ
                   + 2.0f * (q.x * q.z - q.w * q.y) * dirX
                   + 2.0f * (q.y * q.z + q.w * q.x) * dirY;
    const threadmaxx::Vec3 tip{
        swordT->position.x + vx,
        swordT->position.y + vy,
        swordT->position.z + vz,
    };

    const auto& hash = brain_->spatialHash();
    auto& chDamage = engine_->events<DamageDealt>();
    hash.forEachInRadius(tip, kSwordTipRadius,
        [&](const threadmaxx::Vec3&, const threadmaxx::EntityHandle& target) {
            if (!target.valid() || target == player) return;
            if (target == sword) return;  // don't hit yourself
            if (!w.alive(target)) return;
            const threadmaxx::Faction* f = w.tryGetFaction(target);
            if (!f || f->id != kFactionHostile) return;
            if (!w.has<threadmaxx::Health>(target)) return;
            const auto& hp = w.get<threadmaxx::Health>(target);
            if (hp.current <= 0.0f) return;  // already dead

            DamageDealt ev;
            ev.attacker = player;
            ev.target   = target;
            ev.amount   = kSwordDamage;
            const threadmaxx::Transform* tt = w.tryGetTransform(target);
            if (tt) {
                ev.posX = tt->position.x;
                ev.posY = tt->position.y;
                ev.posZ = tt->position.z;
            }
            chDamage.emit(ev);
        });
}

} // namespace rpg
