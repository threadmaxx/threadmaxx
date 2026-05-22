#include "CombatSystem.hpp"

#include "NPCBrainSystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#if RPG_DEMO_HAS_SIMD
#  include <threadmaxx_simd/vec3_ops.hpp>
#endif

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

    // Rising-edge detection: only emit damage on the tick the swing
    // STARTS. Prevents one swing from damaging the same NPC every
    // tick of its 0.30 s window.
    const float currT = ps->swordSwingTimer;
    const bool  rising = (prevSwingTimer_ <= 0.0f && currT > 0.0f);
    prevSwingTimer_ = currT;
    if (!rising) return;

    const threadmaxx::Transform* pT = w.tryGetTransform(player);
    if (!pT) return;
    const SwordTag* tag =
        threadmaxx::user::tryGet<SwordTag>(w, ids_->swordTag, sword);
    const float length = tag ? tag->length : 1.4f;

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

    // 2026-05-20 (rev 2) — combat-side candidate scan replaces the
    // previous query against `NPCBrainSystem::spatialHash`. The hash
    // was rebuilt every preStep (~10 ms at 100k NPCs) but only used
    // on the rare rising-edge tick — net waste. We now do a direct
    // chunk-walk over hostile-NPC chunks limited to entities within
    // a coarse swing-reach radius, then do the precise per-sample
    // tip overlap test against just those candidates.
    //
    // The coarse radius is the worst-case tip-extent: roughly
    // |kSwordRest| + length + kSwordTipRadius. For the demo's
    // constants that's ~2.5 m around the player. At density up to
    // 100k / (60×60) we'd expect ~70 candidates per swing on average,
    // an order of magnitude smaller than a stitched-view sweep.
    const float coarseR = std::sqrt(kSwordRestX * kSwordRestX +
                                    kSwordRestY * kSwordRestY +
                                    kSwordRestZ * kSwordRestZ)
                          + length + kSwordTipRadius + 0.5f;
    const float coarseRSq = coarseR * coarseR;

    struct Candidate {
        threadmaxx::EntityHandle entity;
        threadmaxx::Vec3         pos;
    };
    std::vector<Candidate> cand;
    cand.reserve(64);

#if RPG_DEMO_HAS_SIMD
    // Per-chunk SIMD-batched diff: same shape as the brain's
    // batch — stage positions, broadcast player pos, run
    // `simd::sub`, then scalar inspect the diffs.
    std::vector<threadmaxx::Vec3> positions, playerBroadcast, diffs;
#endif

    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(threadmaxx::Component::Faction)) continue;
        if (!chunk.mask.has(threadmaxx::Component::Health))  continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto n = chunk.entities.size();
        if (n == 0) continue;
#if RPG_DEMO_HAS_SIMD
        positions.resize(n);
        playerBroadcast.assign(n, pT->position);
        diffs.resize(n);
        for (std::size_t r = 0; r < n; ++r) {
            positions[r] = chunk.transforms[r].position;
        }
        threadmaxx::simd::sub(
            std::span<const threadmaxx::Vec3>(positions),
            std::span<const threadmaxx::Vec3>(playerBroadcast),
            std::span<threadmaxx::Vec3>(diffs));
        for (std::size_t r = 0; r < n; ++r) {
            if (chunk.factions[r].id != kFactionHostile) continue;
            if (chunk.healths[r].current <= 0.0f) continue;
            const auto& d = diffs[r];
            if (d.x * d.x + d.y * d.y + d.z * d.z > coarseRSq) continue;
            cand.push_back({chunk.entities[r], chunk.transforms[r].position});
        }
#else
        // ---- Non-SIMD reference path. Same logic without the
        //      batched `simd::sub`.
        for (std::size_t r = 0; r < n; ++r) {
            if (chunk.factions[r].id != kFactionHostile) continue;
            if (chunk.healths[r].current <= 0.0f) continue;
            const auto& tp = chunk.transforms[r].position;
            const float dx = tp.x - pT->position.x;
            const float dy = tp.y - pT->position.y;
            const float dz = tp.z - pT->position.z;
            if (dx * dx + dy * dy + dz * dz > coarseRSq) continue;
            cand.push_back({chunk.entities[r], tp});
        }
#endif
    }
    if (cand.empty()) return;

    auto& chDamage = engine_->events<DamageDealt>();
    std::vector<threadmaxx::EntityHandle> alreadyHit;
    alreadyHit.reserve(8);
    auto wasHit = [&](threadmaxx::EntityHandle e) {
        for (auto x : alreadyHit) if (x == e) return true;
        return false;
    };

    // 2026-05-22 audit (round 2) — close-range guaranteed-hit pass.
    // The tip-arc samples below test small sphere overlaps at the
    // far end of the blade; an enemy that has closed inside the
    // swing radius (e.g. NPC at 0.3 m) falls between the swing arc
    // and the player's body, missing every sample. We add a
    // player-centric overlap test: any candidate within
    // `kNearHitRadius` of the player's position counts as a hit on
    // every swing regardless of blade-tip geometry. Same per-hit
    // bookkeeping (`alreadyHit`) prevents double-billing alongside
    // the tip pass.
    const float nearR2 = kNearHitRadius * kNearHitRadius;
    for (const auto& c : cand) {
        const float dx = c.pos.x - pT->position.x;
        const float dy = c.pos.y - pT->position.y;
        const float dz = c.pos.z - pT->position.z;
        if (dx * dx + dy * dy + dz * dz > nearR2) continue;
        if (wasHit(c.entity)) continue;
        DamageDealt ev;
        ev.attacker = player;
        ev.target   = c.entity;
        ev.amount   = kSwordDamage;
        ev.posX = c.pos.x; ev.posY = c.pos.y; ev.posZ = c.pos.z;
        chDamage.emit(ev);
        alreadyHit.push_back(c.entity);
    }

    const float tipR2 = kSwordTipRadius * kSwordTipRadius;
    // Sample positions along the player-local X-axis chop arc. Same
    // arc as the visible animation in PlayerInputSystem.
    for (int i = 0; i < kSwingHitSamples; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(kSwingHitSamples - 1);
        const float a  = kSwingAngleStart +
                         t * (kSwingAngleEnd - kSwingAngleStart);
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        const float bladeY =  length * sa;
        const float bladeZ = -length * ca;
        const float lx = kSwordRestX;
        const float ly = kSwordRestY + bladeY;
        const float lz = kSwordRestZ + bladeZ;
        const auto offset = rotateByPlayer(lx, ly, lz);
        const threadmaxx::Vec3 tip{
            pT->position.x + offset.x,
            pT->position.y + offset.y,
            pT->position.z + offset.z,
        };
        for (const auto& c : cand) {
            if (wasHit(c.entity)) continue;
            const float dx = c.pos.x - tip.x;
            const float dy = c.pos.y - tip.y;
            const float dz = c.pos.z - tip.z;
            if (dx * dx + dy * dy + dz * dz > tipR2) continue;
            DamageDealt ev;
            ev.attacker = player;
            ev.target   = c.entity;
            ev.amount   = kSwordDamage;
            ev.posX = c.pos.x; ev.posY = c.pos.y; ev.posZ = c.pos.z;
            chDamage.emit(ev);
            alreadyHit.push_back(c.entity);
        }
    }
}

} // namespace rpg
