#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/System.hpp>

namespace rpg {

/// §3.11.9 batch D9 — ages out short-lived particle entities. Reads
/// the `Particle` UserComponent on every chunk that carries it, derives
/// remaining lifetime from `(tick * dt) - spawnTimeSeconds`, and emits
/// `cb.destroy(handle)` for expired entries. Movement is left to the
/// engine's `MovementSystem` since particles spawn with the built-in
/// `Velocity` component; fade is left to a future polish pass.
///
/// Scheduling: reads = {} (the UC bit is not in `ComponentSet` mask
/// space for scheduling — `Transform` is the only relevant built-in,
/// which we don't actually read), writes = {} (we only emit `destroy`
/// which targets the EntityStructural category implicitly). Lands in
/// the first wave alongside any other read-only/destroy-only system.
class ParticleSystem : public threadmaxx::ISystem {
public:
    ParticleSystem(threadmaxx::Engine* engine,
                   const UserComponentIds* ids) noexcept
        : engine_(engine), ids_(ids) {}

    const char* name() const noexcept override { return "particle"; }

    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    /// Cosmetic — runs first in normal play, dropped first under tick-
    /// budget pressure (so the combat path's burst spawns aren't
    /// matched by aging-out the older ones; the engine will catch up
    /// the next non-skipped tick).
    bool skippable() const noexcept override { return true; }

    void update(threadmaxx::SystemContext& ctx) override;

private:
    threadmaxx::Engine*     engine_ = nullptr;
    const UserComponentIds* ids_    = nullptr;
};

} // namespace rpg
