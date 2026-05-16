// §3.11.1 batch D1 — applies queued damage in submission order.
//
// `preStep` drains the typed `DamageDealt` event channel and accumulates
// per-target damage into a scratch map (`damageThisTick_`). `update`
// then applies the accumulated HP changes in a single serial command-
// buffer pass: writes `Health.current -= amount` via `cb.setHealth`,
// and on the kill blow emits an `EntityDied` event on the typed
// channel for RespawnSystem to react to.
//
// Done in two phases (drain → apply) so that multiple hits on the same
// target in one tick compose into one Health write — no chance of
// commit-order shenanigans.

#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

#include <unordered_map>

namespace threadmaxx { class Engine; }

namespace rpg {

class DamageSystem : public threadmaxx::ISystem {
public:
    DamageSystem(threadmaxx::Engine* engine, UserComponentIds* ids)
        : engine_(engine), ids_(ids) {}

    const char* name() const noexcept override { return "damage"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Health}
             | threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Health};
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& ctx) override;

private:
    struct PendingHit {
        threadmaxx::EntityHandle attacker;
        float                    amount = 0.0f;
        float                    posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    };
    threadmaxx::Engine* engine_ = nullptr;
    UserComponentIds*   ids_    = nullptr;
    /// Per-tick accumulator (preserved across ticks; clear at start).
    std::unordered_map<threadmaxx::EntityHandle, PendingHit> damageThisTick_;
};

} // namespace rpg
