#include "ShipLifecycleSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

namespace tou2d {

ShipLifecycleSystem::ShipLifecycleSystem(UserComponentIds ids) noexcept
    : ids_(ids) {}

void ShipLifecycleSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsShip = ids_.ship;
    if (!idsShip.valid()) return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsShip.componentBit()))             continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))    continue;

            const bool disabled =
                chunk.mask.has(threadmaxx::Component::DisabledTag);

            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const auto entities = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                Ship ship = shipSpan[row];

                if (!disabled) {
                    if (ship.currentHp > 0.0f) continue;

                    // ---- Begin death --------------------------------
                    ship.respawnIn = kRespawnTicks;
                    threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);

                    threadmaxx::Velocity v = velocities[row];
                    v.linear  = {0.0f, 0.0f, 0.0f};
                    v.angular = {0.0f, 0.0f, 0.0f};
                    cb.setVelocity(entities[row], v);

                    cb.addTag(entities[row], threadmaxx::Component::DisabledTag);
                    continue;
                }

                // ---- Disabled chunk: tick down respawn ---------------
                if (ship.respawnIn > 1) {
                    ship.respawnIn = static_cast<std::uint16_t>(ship.respawnIn - 1);
                    threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);
                    continue;
                }

                // respawnIn == 0 inside a Disabled chunk shouldn't
                // happen in normal flow, but treat it the same as the
                // last-tick case so we self-heal.
                threadmaxx::Transform t = positions[row];
                t.position.x = ship.spawnX;
                t.position.y = ship.spawnY;
                t.position.z = 0.0f;
                cb.setTransform(entities[row], t);

                threadmaxx::Velocity v{};
                cb.setVelocity(entities[row], v);

                ship.currentHp = ship.maxHp;
                ship.respawnIn = 0;
                threadmaxx::addUserComponent(cb, idsShip, entities[row], ship);

                cb.removeTag(entities[row], threadmaxx::Component::DisabledTag);
            }
        }
    });
}

} // namespace tou2d
