#include "RespawnSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <algorithm>
#include <utility>

namespace rpg {

void RespawnSystem::preStep(threadmaxx::SystemContext&) {
    drops_.clear();
    auto evs = engine_->events<EntityDied>().drainTick();
    for (const auto& d : evs) {
        DropPlan plan;
        plan.corpse = d.entity;
        plan.posX   = d.posX;
        plan.posY   = std::max(d.posY, 0.4f);
        plan.posZ   = d.posZ;
        // §3.5 batch 2: reserve the pickup handle on the sim thread
        // so the spawn command in `update` has a stable handle ahead
        // of commit. Any reservation not consumed is reaped by the
        // engine at step end.
        plan.pickup = engine_->reserveEntityHandle();
        drops_.push_back(plan);
    }
}

void RespawnSystem::update(threadmaxx::SystemContext& ctx) {
    if (drops_.empty()) return;
    const auto idsCube   = ids_->cubeRender;
    const auto idsPickup = ids_->pickup;
    auto plans = drops_;  // copy by value into the lambda

    ctx.single([plans = std::move(plans), idsCube, idsPickup]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (const auto& p : plans) {
            // §3.11.1 batch D1 — corpse: flip DisabledTag so the
            // renderer skips it and the spatial hash query (next
            // tick) loses interest in it.
            cb.addTag(p.corpse, threadmaxx::Component::DisabledTag);

            // Spawn a gold pickup at the death location into the
            // pre-reserved handle.
            threadmaxx::Bundle b{};
            b.transform.position = {p.posX, p.posY, p.posZ};
            b.transform.scale    = {0.4f, 0.4f, 0.4f};
            b.faction.id         = kFactionNeutral;
            b.boundingVolume.min = {p.posX - 0.4f, p.posY - 0.4f, p.posZ - 0.4f};
            b.boundingVolume.max = {p.posX + 0.4f, p.posY + 0.4f, p.posZ + 0.4f};
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Faction,
                threadmaxx::Component::BoundingVolume,
            };
            cb.spawnBundle(p.pickup, b);
            threadmaxx::addUserComponent(cb, idsCube, p.pickup,
                CubeRender{{1.0f, 0.85f, 0.20f, 1.0f}, 0.7f, {0,0,0}});
            threadmaxx::addUserComponent(cb, idsPickup, p.pickup,
                Pickup{2u});  // killed-NPC drop = 2x value of floor pickup
        }
    });
}

} // namespace rpg
