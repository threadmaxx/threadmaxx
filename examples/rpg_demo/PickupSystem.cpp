#include "PickupSystem.hpp"

#include "NPCBrainSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <vector>

namespace rpg {

void PickupSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    const auto player = worldState_->player;
    if (!player.valid() || !w.alive(player)) return;

    const auto& pT = w.get<threadmaxx::Transform>(player);
    constexpr float kPickRadius = 1.2f;

    const auto& hash = brain_->spatialHash();
    std::vector<threadmaxx::EntityHandle> hits;
    hits.reserve(8);

    hash.forEachInRadius(pT.position, kPickRadius,
        [&](const threadmaxx::Vec3&, threadmaxx::EntityHandle e) {
            if (e == player) return;
            if (!w.alive(e)) return;
            if (w.hasTag(e, threadmaxx::Component::DisabledTag)) return;
            if (!threadmaxx::user::has(w, ids_->pickup, e)) return;
            hits.push_back(e);
        });

    if (hits.empty()) return;

    // Read the player's pickup count, increment, and queue the writes
    // + events. The single() callback below is the only thing that
    // mutates the player's user-component this tick.
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

    // Publish on the engine's typed channel; HUD subscribes in postStep.
    auto& ch = engine_->events<PickupCollected>();
    for (const auto& ev : events) ch.emit(ev);
}

} // namespace rpg
