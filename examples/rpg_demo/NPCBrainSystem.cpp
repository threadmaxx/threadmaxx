#include "NPCBrainSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>
#include <random>

namespace rpg {

namespace {

constexpr float kCellSize     = 4.0f;
constexpr float kWanderDur    = 2.5f;
constexpr float kIdleDur      = 1.5f;
constexpr float kFleeSpeed    = 4.5f;
constexpr float kWanderSpeed  = 2.0f;
// §3.11.1 batch D1 — combat-state tuning.
constexpr float kChargeSpeed  = 3.5f;
constexpr float kRetreatSpeed = 5.0f;
constexpr float kRetreatDur   = 4.0f;
constexpr float kRetreatHpFrac = 0.30f;
constexpr float kFightStopDist = 1.0f;  // don't overshoot the player

inline float distanceXZ(const threadmaxx::Vec3& a, const threadmaxx::Vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

NPCBrainSystem::NPCBrainSystem(WorldState* worldState, UserComponentIds* ids)
    : worldState_(worldState), ids_(ids), hash_(kCellSize) {}

void NPCBrainSystem::preStep(threadmaxx::SystemContext& ctx) {
    // Rebuild the spatial hash from every entity with a BoundingVolume.
    // Single-threaded — the hash is not thread-safe and we want every
    // wave system (including the downstream PickupSystem) to see the
    // same view.
    hash_.clear();
    const auto& w = ctx.world();
    const auto entities = w.entities();
    const auto transforms = w.transforms();
    const auto masks = w.componentMasks();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!masks[i].has(threadmaxx::Component::BoundingVolume)) continue;
        if (masks[i].has(threadmaxx::Component::DisabledTag))       continue;
        hash_.insert(transforms[i].position, entities[i]);
    }
}

void NPCBrainSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    const auto player = worldState_->player;
    if (!player.valid() || !w.alive(player)) return;

    const auto& playerT = w.get<threadmaxx::Transform>(player);
    const threadmaxx::Vec3 pp = playerT.position;
    const float dt = static_cast<float>(ctx.dt());

    // Snapshot the stitched entity + mask views once. parallelFor jobs
    // capture these by value (cheap span copies); the underlying
    // vectors are stable for the duration of this `update()` because
    // no system mutates them outside the commit phase.
    const auto entities = w.entities();
    const auto masks    = w.componentMasks();
    const std::uint32_t count = static_cast<std::uint32_t>(entities.size());

    // Per-job RNG is seeded deterministically from tick + range start
    // so two runs of the same scene produce the same NPC paths. The
    // shared `rng_` member is no longer used (worker-shared mutable
    // state would have raced).
    auto* ids = ids_;
    const std::uint64_t tickSalt = ctx.tick();

    ctx.parallelFor(count, /*grain*/ 0,
        [&ctx, &w, ids, pp, dt, tickSalt, entities, masks]
        (threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            std::mt19937 rng(static_cast<std::uint32_t>(
                0xC0FFEEu ^ tickSalt ^ static_cast<std::uint64_t>(r.begin)));
            std::uniform_real_distribution<float> ang(-3.14159f, 3.14159f);
            std::uniform_real_distribution<float> dist(3.0f, 10.0f);

            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                if ((i & 0x1FFu) == 0 && ctx.shouldYield()) return;
                if (!masks[i].has(threadmaxx::Component::Faction))    continue;
                if (masks[i].has(threadmaxx::Component::DisabledTag)) continue;

                const auto e = entities[i];
                const NpcState* st = threadmaxx::user::tryGet<NpcState>(w, ids->npcState, e);
                if (!st) continue;

                const auto& fac = w.get<threadmaxx::Faction>(e);
                if (fac.id != kFactionHostile && fac.id != kFactionFriendly) continue;

                const auto& tr = w.get<threadmaxx::Transform>(e);
                const float distToPlayer = distanceXZ(tr.position, pp);

                NpcState next = *st;
                next.stateTimer += dt;
                threadmaxx::Vec3 vel{0, 0, 0};

                const bool hostile = (fac.id == kFactionHostile);
                const threadmaxx::Health* hp = w.tryGetHealth(e);
                const bool lowHp = hp && hp->max > 0.0f &&
                                   (hp->current / hp->max) < kRetreatHpFrac;

                switch (next.mode) {
                case NpcState::Idle:
                    if (distToPlayer < next.aoiRadius) {
                        next.mode       = hostile ? NpcState::Fight
                                                  : NpcState::Flee;
                        next.stateTimer = 0.0f;
                    } else if (next.stateTimer >= kIdleDur) {
                        const float a = ang(rng);
                        const float d = dist(rng);
                        next.targetX   = tr.position.x + std::cos(a) * d;
                        next.targetZ   = tr.position.z + std::sin(a) * d;
                        next.mode      = NpcState::Wander;
                        next.stateTimer = 0.0f;
                    }
                    break;

                case NpcState::Wander: {
                    const float dx = next.targetX - tr.position.x;
                    const float dz = next.targetZ - tr.position.z;
                    const float d  = std::sqrt(dx * dx + dz * dz);
                    if (distToPlayer < next.aoiRadius) {
                        next.mode = hostile ? NpcState::Fight : NpcState::Flee;
                        next.stateTimer = 0.0f;
                    } else if (next.stateTimer >= kWanderDur || d < 0.5f) {
                        next.mode = NpcState::Idle;
                        next.stateTimer = 0.0f;
                    } else {
                        vel.x = (dx / d) * kWanderSpeed;
                        vel.z = (dz / d) * kWanderSpeed;
                    }
                    break;
                }

                case NpcState::Flee: {
                    const float dx = tr.position.x - pp.x;
                    const float dz = tr.position.z - pp.z;
                    const float d  = std::sqrt(dx * dx + dz * dz);
                    if (distToPlayer > next.aoiRadius * 1.8f) {
                        next.mode = NpcState::Idle;
                        next.stateTimer = 0.0f;
                    } else if (d > 0.01f) {
                        vel.x = (dx / d) * kFleeSpeed;
                        vel.z = (dz / d) * kFleeSpeed;
                    }
                    break;
                }

                case NpcState::Fight: {
                    if (lowHp) {
                        next.mode = NpcState::Retreat;
                        next.stateTimer = 0.0f;
                    } else if (distToPlayer > next.aoiRadius * 1.8f) {
                        next.mode = NpcState::Wander;
                        next.stateTimer = 0.0f;
                        next.targetX = pp.x; next.targetZ = pp.z;
                    } else if (distToPlayer > kFightStopDist) {
                        const float dx = pp.x - tr.position.x;
                        const float dz = pp.z - tr.position.z;
                        const float d  = std::sqrt(dx * dx + dz * dz);
                        if (d > 0.01f) {
                            vel.x = (dx / d) * kChargeSpeed;
                            vel.z = (dz / d) * kChargeSpeed;
                        }
                    }
                    break;
                }

                case NpcState::Retreat: {
                    if (next.stateTimer >= kRetreatDur) {
                        next.mode = NpcState::Wander;
                        next.stateTimer = 0.0f;
                        next.targetX = tr.position.x + (tr.position.x - pp.x);
                        next.targetZ = tr.position.z + (tr.position.z - pp.z);
                    } else {
                        const float dx = tr.position.x - pp.x;
                        const float dz = tr.position.z - pp.z;
                        const float d  = std::sqrt(dx * dx + dz * dz);
                        if (d > 0.01f) {
                            vel.x = (dx / d) * kRetreatSpeed;
                            vel.z = (dz / d) * kRetreatSpeed;
                        }
                    }
                    break;
                }
                }

                // Write directly into the per-job CommandBuffer. `addUserComponent`
                // on an entity that already carries the bit is an in-place
                // memcpy of the column row — no archetype migration. Dropping
                // the prior `removeUserComponent + addUserComponent` pair
                // saves two migrations per NPC per tick (huge under stress).
                cb.setVelocity(e, threadmaxx::Velocity{vel, {0, 0, 0}});
                threadmaxx::addUserComponent(cb, ids->npcState, e, next);
            }
        });
}

} // namespace rpg
