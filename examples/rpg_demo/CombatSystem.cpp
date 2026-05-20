#include "CombatSystem.hpp"

#include "NPCBrainSystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>
#include <vector>

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

    // 2026-05-20 — drive the hit check from the PLAYER's world
    // transform plus the same swing-arc constants
    // PlayerInputSystem uses to animate the sword. Pre-fix this
    // read the sword's CURRENT world transform; that worked
    // fine while the sword was static but as soon as the swing
    // animation landed (same date), the rising-edge tick caught
    // the sword at its starting angle (≈ +1.1 rad), placing
    // the tip far from any frontal target. The user reported
    // "NPCs are not hit". Sampling `kSwingSamples` tips along
    // the full swing arc gives a swept-volume hit check that
    // matches the visible sweep, and stays decoupled from the
    // animation timing.
    const threadmaxx::Transform* pT = w.tryGetTransform(player);
    if (!pT) return;
    const SwordTag* tag =
        threadmaxx::user::tryGet<SwordTag>(w, ids_->swordTag, sword);
    const float length = tag ? tag->length : 1.4f;

    constexpr float kRestX = 0.5f;
    constexpr float kRestY = 0.8f;
    constexpr float kRestZ = -0.8f;
    constexpr float kSwingArc     = 2.2f;  // matches PlayerInputSystem
    constexpr int   kSwingSamples = 5;

    const auto& q = pT->orientation;
    auto rotateByPlayer = [&](float dx, float dy, float dz)
            -> threadmaxx::Vec3 {
        const float vx = 2.0f * (q.x * q.z + q.w * q.y) * dz
                       + (1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * dx
                       + 2.0f * (q.x * q.y - q.w * q.z) * dy;
        const float vy = 2.0f * (q.y * q.z - q.w * q.x) * dz
                       + 2.0f * (q.x * q.y + q.w * q.z) * dx
                       + (1.0f - 2.0f * (q.x * q.x + q.z * q.z)) * dy;
        const float vz = (1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * dz
                       + 2.0f * (q.x * q.z - q.w * q.y) * dx
                       + 2.0f * (q.y * q.z + q.w * q.x) * dy;
        return {vx, vy, vz};
    };

    const auto& hash = brain_->spatialHash();
    auto& chDamage = engine_->events<DamageDealt>();

    std::vector<threadmaxx::EntityHandle> alreadyHit;
    alreadyHit.reserve(8);
    auto wasHit = [&](threadmaxx::EntityHandle e) {
        for (auto x : alreadyHit) if (x == e) return true;
        return false;
    };

    for (int i = 0; i < kSwingSamples; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(kSwingSamples - 1);  // 0..1
        const float a  = (0.5f - t) * kSwingArc;
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        // Resting pivot offset (kRestX, kRestY, kRestZ) plus the
        // blade vector (0, 0, -length), both rotated by `a` around
        // the player-local +Y axis. Stays in the player-local frame
        // until the final `rotateByPlayer` lift into world space.
        const float lx = kRestX * ca - kRestZ * sa - length * sa;
        const float ly = kRestY;
        const float lz = kRestX * sa + kRestZ * ca - length * ca;
        const auto offset = rotateByPlayer(lx, ly, lz);
        const threadmaxx::Vec3 tip{
            pT->position.x + offset.x,
            pT->position.y + offset.y,
            pT->position.z + offset.z,
        };
        hash.forEachInRadius(tip, kSwordTipRadius,
            [&](const threadmaxx::Vec3&,
                const threadmaxx::EntityHandle& target) {
                if (!target.valid() || target == player) return;
                if (target == sword) return;
                if (!w.alive(target)) return;
                if (wasHit(target)) return;
                const threadmaxx::Faction* f = w.tryGetFaction(target);
                if (!f || f->id != kFactionHostile) return;
                if (!w.has<threadmaxx::Health>(target)) return;
                const auto& hp = w.get<threadmaxx::Health>(target);
                if (hp.current <= 0.0f) return;
                DamageDealt ev;
                ev.attacker = player;
                ev.target   = target;
                ev.amount   = kSwordDamage;
                const threadmaxx::Transform* tt =
                    w.tryGetTransform(target);
                if (tt) {
                    ev.posX = tt->position.x;
                    ev.posY = tt->position.y;
                    ev.posZ = tt->position.z;
                }
                chDamage.emit(ev);
                alreadyHit.push_back(target);
            });
    }
}

} // namespace rpg
