#include "NPCBrainSystem.hpp"

#include "ParallelDispatch.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#if RPG_DEMO_HAS_SIMD
#  include <threadmaxx_simd/vec3_ops.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rpg {

namespace {

constexpr float kWanderDur    = 2.5f;
constexpr float kIdleDur      = 1.5f;
constexpr float kFleeSpeed    = 4.5f;
constexpr float kWanderSpeed  = 2.0f;
constexpr float kChargeSpeed  = 3.5f;
constexpr float kRetreatSpeed = 2.8f;
constexpr float kRetreatDur   = 4.0f;
constexpr float kRetreatHpFrac = 0.30f;
constexpr float kFightStopDist = 1.0f;

/// Tiny deterministic xorshift32 PRNG. Replaces `std::mt19937` from
/// the legacy path — same seed → same sequence, but constructed in
/// O(1) instead of mt19937's 2.5 kB state init.
struct XorShift32 {
    std::uint32_t state;
    constexpr explicit XorShift32(std::uint32_t seed) noexcept
        : state(seed ? seed : 0x9e3779b9u) {}
    constexpr std::uint32_t next() noexcept {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    float angle() noexcept {
        return (static_cast<float>(next()) * (1.0f / 4294967296.0f) - 0.5f)
               * 6.2831853f;
    }
    float wanderDist() noexcept {
        return 3.0f + static_cast<float>(next()) * (7.0f / 4294967296.0f);
    }
};

struct BrainSlice {
    const threadmaxx::internal::ArchetypeChunk* chunk;
    std::span<const NpcState>                   npcSpan;
    std::uint32_t                               beginFlat;
    std::uint32_t                               endFlat;
};

} // namespace

NPCBrainSystem::NPCBrainSystem(threadmaxx::Engine* engine,
                               WorldState* worldState, UserComponentIds* ids)
    : engine_(engine), worldState_(worldState), ids_(ids) {}

void NPCBrainSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    const auto player = worldState_->player;
    if (!player.valid() || !w.alive(player)) return;

    const auto& playerT = w.get<threadmaxx::Transform>(player);
    const threadmaxx::Vec3 pp = playerT.position;
    const float dt = static_cast<float>(ctx.dt());

    // 2026-05-20 (rev 4) — flat-row parallelFor with cross-chunk slices.
    //
    // Earlier revs used per-chunk-outer / row-parallel-inner — the
    // outer loop's `parallelFor(rows, grain=0)` deadlocked when any
    // NPC chunk had a small row count (parallelFor submits 1 sub-job
    // → round-robin'd to a single worker → other workers never get
    // notified, wave latch deadlocks).
    //
    // This rev does ONE big `parallelFor(totalNpcRows, grain=0)` over
    // the flat row count across every matching NPC chunk. Always
    // produces ~4*workerCount sub-jobs.
    //
    // The chunk-level SIMD diff stage runs serially before the big
    // parallelFor — one `simd::sub` call over the full packed
    // position buffer subtracts the (broadcast) player position.

    const auto pickupBit = ids_->pickup.componentBit();
    const auto npcId     = ids_->npcState;
    const auto npcBit    = npcId.componentBit();
    const auto chunkCount = w.archetypeChunkCount();

    // Build slice list + packed positions / player-broadcast buffers.
    std::vector<BrainSlice>       slices;
    std::vector<threadmaxx::Vec3> positions;
    std::vector<threadmaxx::Vec3> playerBroadcast;
    slices.reserve(8);
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(threadmaxx::Component::Faction)) continue;
        if (!chunk.mask.has(npcBit))                         continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        if (chunk.mask.has(pickupBit))                       continue;
        const auto n = chunk.entities.size();
        if (n == 0) continue;

        BrainSlice s;
        s.chunk     = &chunk;
        s.npcSpan   = threadmaxx::user::chunkSpan<NpcState>(chunk, npcId);
        if (s.npcSpan.empty()) continue;
        s.beginFlat = static_cast<std::uint32_t>(positions.size());
        for (std::size_t r = 0; r < n; ++r) {
            positions.push_back(chunk.transforms[r].position);
        }
        s.endFlat   = static_cast<std::uint32_t>(positions.size());
        slices.push_back(s);
    }
    const std::uint32_t total = static_cast<std::uint32_t>(positions.size());
    if (total == 0) return;

    playerBroadcast.assign(total, pp);
    std::vector<threadmaxx::Vec3> diffs(total);
#if RPG_DEMO_HAS_SIMD
    // SIMD-batched (pos - player) across ALL chunks in one call.
    threadmaxx::simd::sub(
        std::span<const threadmaxx::Vec3>(positions),
        std::span<const threadmaxx::Vec3>(playerBroadcast),
        std::span<threadmaxx::Vec3>(diffs));
#else
    // ---- Non-SIMD reference path.
    for (std::uint32_t i = 0; i < total; ++i) {
        diffs[i].x = positions[i].x - pp.x;
        diffs[i].y = positions[i].y - pp.y;
        diffs[i].z = positions[i].z - pp.z;
    }
#endif

    auto& damageChan = engine_->events<DamageDealt>();
    const auto playerHandle = player;
    const std::uint64_t tickSalt = ctx.tick();
    const auto npcIdLocal = npcId;
    const auto* slicesPtr = slices.data();
    const std::uint32_t sliceCount =
        static_cast<std::uint32_t>(slices.size());
    const auto* diffsData = diffs.data();

    dispatchOrInline(ctx, total,
        [&ctx, slicesPtr, sliceCount, diffsData, npcIdLocal,
         pp, dt, tickSalt, playerHandle, &damageChan]
        (threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            // Find starting slice for r.begin via linear scan.
            std::uint32_t si = 0;
            while (si + 1 < sliceCount && r.begin >= slicesPtr[si].endFlat) {
                ++si;
            }
            XorShift32 rng(0xC0FFEEu ^
                           static_cast<std::uint32_t>(tickSalt) ^
                           r.begin * 0x9e3779b9u);

            for (std::uint32_t flat = r.begin; flat < r.end; ++flat) {
                if ((flat & 0x1FFu) == 0 && ctx.shouldYield()) return;
                while (si + 1 < sliceCount && flat >= slicesPtr[si].endFlat) {
                    ++si;
                }
                const auto& slice = slicesPtr[si];
                const std::uint32_t row = flat - slice.beginFlat;
                const auto* chunk = slice.chunk;

                const auto& fac = chunk->factions[row];
                if (fac.id != kFactionHostile && fac.id != kFactionFriendly) continue;

                const auto& tr   = chunk->transforms[row];
                const auto& diff = diffsData[flat];
                const float distToPlayerSq = diff.x * diff.x + diff.z * diff.z;
                float distToPlayer = -1.0f;
                auto distXZ = [&]() {
                    if (distToPlayer < 0.0f)
                        distToPlayer = std::sqrt(distToPlayerSq);
                    return distToPlayer;
                };

                const NpcState st = slice.npcSpan[row];
                NpcState next = st;
                next.stateTimer += dt;
                if (next.attackCooldown > 0.0f) {
                    next.attackCooldown =
                        std::max(0.0f, next.attackCooldown - dt);
                }
                threadmaxx::Vec3 vel{0, 0, 0};

                const bool hostile = (fac.id == kFactionHostile);
                const threadmaxx::Health* hp =
                    chunk->mask.has(threadmaxx::Component::Health)
                        ? &chunk->healths[row] : nullptr;
                const bool lowHp = hp && hp->max > 0.0f &&
                                   (hp->current / hp->max) < kRetreatHpFrac;

                switch (next.mode) {
                case NpcState::Idle: {
                    const float aoiSq = next.aoiRadius * next.aoiRadius;
                    if (distToPlayerSq < aoiSq) {
                        next.mode       = hostile ? NpcState::Fight
                                                  : NpcState::Flee;
                        next.stateTimer = 0.0f;
                    } else if (next.stateTimer >= kIdleDur) {
                        const float a = rng.angle();
                        const float d = rng.wanderDist();
                        next.targetX   = tr.position.x + std::cos(a) * d;
                        next.targetZ   = tr.position.z + std::sin(a) * d;
                        next.mode      = NpcState::Wander;
                        next.stateTimer = 0.0f;
                    }
                    break;
                }
                case NpcState::Wander: {
                    const float dx = next.targetX - tr.position.x;
                    const float dz = next.targetZ - tr.position.z;
                    const float d2 = dx * dx + dz * dz;
                    const float aoiSq = next.aoiRadius * next.aoiRadius;
                    if (distToPlayerSq < aoiSq) {
                        next.mode = hostile ? NpcState::Fight
                                            : NpcState::Flee;
                        next.stateTimer = 0.0f;
                    } else if (next.stateTimer >= kWanderDur || d2 < 0.25f) {
                        next.mode = NpcState::Idle;
                        next.stateTimer = 0.0f;
                    } else {
                        const float d = std::sqrt(d2);
                        vel.x = (dx / d) * kWanderSpeed;
                        vel.z = (dz / d) * kWanderSpeed;
                    }
                    break;
                }
                case NpcState::Flee: {
                    const float d = distXZ();
                    if (d > next.aoiRadius * 1.8f) {
                        next.mode = NpcState::Idle;
                        next.stateTimer = 0.0f;
                    } else if (d > 0.01f) {
                        vel.x = (diff.x / d) * kFleeSpeed;
                        vel.z = (diff.z / d) * kFleeSpeed;
                    }
                    break;
                }
                case NpcState::Fight: {
                    const float d = distXZ();
                    if (lowHp && next.fleeRoll < kRetreatChance) {
                        next.mode = NpcState::Retreat;
                        next.stateTimer = 0.0f;
                    } else if (d > next.aoiRadius * 1.8f) {
                        next.mode = NpcState::Wander;
                        next.stateTimer = 0.0f;
                        next.targetX = pp.x; next.targetZ = pp.z;
                    } else if (d > kFightStopDist) {
                        if (d > 0.01f) {
                            vel.x = (-diff.x / d) * kChargeSpeed;
                            vel.z = (-diff.z / d) * kChargeSpeed;
                        }
                    }
                    if (d < kNpcAttackRange &&
                        next.attackCooldown <= 0.0f) {
                        DamageDealt hit;
                        hit.attacker = chunk->entities[row];
                        hit.target   = playerHandle;
                        hit.amount   = kNpcAttackDamage;
                        hit.posX = pp.x; hit.posY = pp.y; hit.posZ = pp.z;
                        damageChan.emit(hit);
                        next.attackCooldown = kNpcAttackCooldown;
                    }
                    break;
                }
                case NpcState::Retreat: {
                    if (next.stateTimer >= kRetreatDur) {
                        next.mode = NpcState::Wander;
                        next.stateTimer = 0.0f;
                        next.targetX = tr.position.x + diff.x;
                        next.targetZ = tr.position.z + diff.z;
                    } else {
                        const float d = distXZ();
                        if (d > 0.01f) {
                            vel.x = (diff.x / d) * kRetreatSpeed;
                            vel.z = (diff.z / d) * kRetreatSpeed;
                        }
                    }
                    break;
                }
                }

                // Skip-when-equal writes — see rev 2 notes.
                NpcState cmpA = next;  cmpA.stateTimer = 0.0f;
                NpcState cmpB = st;    cmpB.stateTimer = 0.0f;
                const bool stateMeaningfullyChanged =
                    std::memcmp(&cmpA, &cmpB, sizeof(NpcState)) != 0;
                if (stateMeaningfullyChanged) {
                    threadmaxx::addUserComponent(cb, npcIdLocal,
                        chunk->entities[row], next);
                }
                const auto& curVel = chunk->velocities[row].linear;
                if (vel.x != curVel.x || vel.y != curVel.y ||
                    vel.z != curVel.z) {
                    cb.setVelocity(chunk->entities[row],
                        threadmaxx::Velocity{vel, {0, 0, 0}});
                }
            }
        });
}

} // namespace rpg
