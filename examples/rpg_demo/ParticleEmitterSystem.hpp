#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/System.hpp>

#include <random>
#include <vector>

namespace rpg {

/// §3.11.9 batch D9 — drains combat / death / pickup events, burst-
/// spawns short-lived particle entities at each event's world position.
/// The actual `cb.spawnBundle` calls run inside one `ctx.single` lambda
/// so the per-tick spawn burst lands as one tight block in the
/// commit phase — which is what stresses the §3.6.3 sharded-commit
/// fast path and the §3.6 batch 30 per-archetype hash rollup at scale.
///
/// `Particle.spawnTimeSeconds` is set to the current `tick * dt` at
/// emit time; `ParticleSystem` reads the same expression each tick
/// and destroys entries whose remaining lifetime hit zero.
class ParticleEmitterSystem : public threadmaxx::ISystem {
public:
    ParticleEmitterSystem(threadmaxx::Engine* engine,
                          const UserComponentIds* ids) noexcept
        : engine_(engine), ids_(ids), rng_(0xD9C0FFEEu) {}

    const char* name() const noexcept override { return "particle-emit"; }

    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    /// Spawns entities, so it writes the EntityStructural category.
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::EntityStructural};
    }

    bool skippable() const noexcept override { return true; }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& ctx) override;

private:
    struct Burst {
        float            x = 0.0f, y = 0.0f, z = 0.0f;
        std::uint32_t    count = 0;
        float            speed       = 2.0f;
        float            lifeSeconds = 0.5f;
        float            color[4]    = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    threadmaxx::Engine*     engine_ = nullptr;
    const UserComponentIds* ids_    = nullptr;
    std::mt19937            rng_;
    std::vector<Burst>      bursts_;
};

} // namespace rpg
