#include "PickupSystem.hpp"

#include "NPCBrainSystem.hpp"

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

#include <vector>

namespace rpg {

void PickupSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    const auto player = worldState_->player;
    if (!player.valid() || !w.alive(player)) return;

    const auto& pT = w.get<threadmaxx::Transform>(player);
    constexpr float kPickRadius = 1.2f;
    constexpr float kPickRadiusSq = kPickRadius * kPickRadius;

    // Direct chunk walk over Pickup-bearing chunks. Pickups live in
    // their own archetype (Transform + Faction + BoundingVolume +
    // Pickup user-component), so the chunk filter is one mask check
    // per archetype.
    const auto pickupBit = ids_->pickup.componentBit();
    std::vector<threadmaxx::EntityHandle> hits;
    hits.reserve(8);

#if RPG_DEMO_HAS_SIMD
    // Per-chunk SIMD-batched diff: stage pickup positions, broadcast
    // the player position, run `simd::sub`, then scalar inspect the
    // diffs for the squared-distance test. At 5k pickups the SIMD
    // sub is ~150 ns vs scalar ~600 ns — the genuine win is the
    // sub-millisecond pickup pass, which used to be a measurable
    // chunk of the per-tick budget at higher pickup counts.
    std::vector<threadmaxx::Vec3> positions, playerBroadcast, diffs;
#endif

    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(pickupBit)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto n = chunk.entities.size();
        if (n == 0) continue;
#if RPG_DEMO_HAS_SIMD
        positions.resize(n);
        playerBroadcast.assign(n, pT.position);
        diffs.resize(n);
        for (std::size_t r = 0; r < n; ++r) {
            positions[r] = chunk.transforms[r].position;
        }
        threadmaxx::simd::sub(
            std::span<const threadmaxx::Vec3>(positions),
            std::span<const threadmaxx::Vec3>(playerBroadcast),
            std::span<threadmaxx::Vec3>(diffs));
        for (std::size_t r = 0; r < n; ++r) {
            const auto& d = diffs[r];
            if (d.x * d.x + d.y * d.y + d.z * d.z > kPickRadiusSq) continue;
            const auto e = chunk.entities[r];
            if (e == player) continue;
            hits.push_back(e);
        }
#else
        // ---- Non-SIMD reference path. Same logic without batched
        //      `simd::sub`; scalar per-row distance check.
        for (std::size_t r = 0; r < n; ++r) {
            const auto& tp = chunk.transforms[r].position;
            const float dx = tp.x - pT.position.x;
            const float dy = tp.y - pT.position.y;
            const float dz = tp.z - pT.position.z;
            if (dx * dx + dy * dy + dz * dz > kPickRadiusSq) continue;
            const auto e = chunk.entities[r];
            if (e == player) continue;
            hits.push_back(e);
        }
#endif
    }

    if (hits.empty()) return;

    const PlayerState* ps =
        threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
    if (!ps) return;
    PlayerState updated = *ps;

    std::vector<PickupCollected> events;
    events.reserve(hits.size());
    for (auto h : hits) {
        const Pickup* pk = threadmaxx::user::tryGet<Pickup>(w, ids_->pickup, h);
        const std::uint32_t value = pk ? pk->value : 1u;
        updated.pickups += value;
        events.push_back(PickupCollected{h, player, value, updated.pickups});
    }

    auto* ids = ids_;
    ctx.single([player, hits = std::move(hits), updated, ids]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (auto h : hits) {
            cb.addTag(h, threadmaxx::Component::DisabledTag);
        }
        threadmaxx::removeUserComponent(cb, ids->playerState, player);
        threadmaxx::addUserComponent(cb, ids->playerState, player, updated);
    });

    auto& ch = engine_->events<PickupCollected>();
    for (const auto& ev : events) ch.emit(ev);
}

} // namespace rpg
