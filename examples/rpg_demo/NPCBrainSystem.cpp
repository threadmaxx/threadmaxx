#include "NPCBrainSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace rpg {

namespace {

constexpr float kCellSize    = 4.0f;
constexpr float kWanderDur   = 2.5f;
constexpr float kIdleDur     = 1.5f;
constexpr float kFleeSpeed   = 4.5f;
constexpr float kWanderSpeed = 2.0f;

inline float distanceXZ(const threadmaxx::Vec3& a, const threadmaxx::Vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

NPCBrainSystem::NPCBrainSystem(WorldState* worldState, UserComponentIds* ids)
    : worldState_(worldState), ids_(ids), hash_(kCellSize), rng_(0xC0FFEEu) {}

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

    // Walk entities serially because we mutate per-NPC user-component
    // state inline (the state machine RNG isn't thread-safe). The total
    // NPC count is small (≤ 50) so parallelism isn't worth it here.
    const auto entities = w.entities();
    const auto masks    = w.componentMasks();

    std::vector<std::tuple<threadmaxx::EntityHandle,
                           threadmaxx::Velocity,
                           NpcState>> pendingWrites;
    pendingWrites.reserve(64);

    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!masks[i].has(threadmaxx::Component::Faction))    continue;
        if (masks[i].has(threadmaxx::Component::DisabledTag)) continue;

        const auto e = entities[i];
        const NpcState* st = threadmaxx::user::tryGet<NpcState>(w, ids_->npcState, e);
        if (!st) continue;

        const auto& fac = w.get<threadmaxx::Faction>(e);
        if (fac.id != kFactionHostile && fac.id != kFactionFriendly) continue;

        const auto& tr = w.get<threadmaxx::Transform>(e);
        const float distToPlayer = distanceXZ(tr.position, pp);

        NpcState next = *st;
        next.stateTimer += dt;
        threadmaxx::Vec3 vel{0, 0, 0};

        switch (next.mode) {
        case NpcState::Idle:
            if (distToPlayer < next.aoiRadius) {
                next.mode       = NpcState::Flee;
                next.stateTimer = 0.0f;
            } else if (next.stateTimer >= kIdleDur) {
                std::uniform_real_distribution<float> ang(-3.14159f, 3.14159f);
                std::uniform_real_distribution<float> dist(3.0f, 10.0f);
                const float a = ang(rng_);
                const float d = dist(rng_);
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
                next.mode = NpcState::Flee;
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
        }

        pendingWrites.emplace_back(e, threadmaxx::Velocity{vel, {0, 0, 0}}, next);
    }

    auto* ids = ids_;
    ctx.single([writes = std::move(pendingWrites), ids]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (auto& [e, vel, st] : writes) {
            cb.setVelocity(e, vel);
            threadmaxx::removeUserComponent(cb, ids->npcState, e);
            threadmaxx::addUserComponent(cb, ids->npcState, e, st);
        }
    });
}

} // namespace rpg
