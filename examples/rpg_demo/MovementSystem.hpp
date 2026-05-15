#pragma once

#include <threadmaxx/System.hpp>

namespace rpg {

/// Integrates `Velocity.linear` into `Transform.position` per tick.
/// Skips entities with `DisabledTag` so picked-up items / defeated NPCs
/// stop moving. Pure data — runs after every system that writes
/// Velocity (PlayerInputSystem, NPCBrainSystem).
class MovementSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "movement"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Velocity,
            threadmaxx::Component::Transform,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    void update(threadmaxx::SystemContext& ctx) override;
};

} // namespace rpg
