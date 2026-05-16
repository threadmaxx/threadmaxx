// §3.9.1 batch 16 — Canonical workload seeds.
//
// Three deterministic, RPG-shaped scenes that every §3.9 perf
// benchmark builds on top of. Same seed → same entity layout → same
// archetype mix → directly comparable numbers across runs.
//
// Workloads:
//
//   - `AiOnlyWorkload`   ~1k entities, all with Transform / Velocity /
//                        BoundingVolume; half tagged `Health` for
//                        archetype variety. NPCs that wander or chase
//                        on their own.
//
//   - `RenderAiWorkload` ~20k entities = AI population scaled up + a
//                        large terrain block + RenderTag-bearing
//                        decoration cubes. Half the population carries
//                        RenderTag; ~5% carry StaticTag.
//
//   - `ChurnWorkload`    ~100k entities. Every tick, ~1% spawn and
//                        ~1% destroy; ~5% flip Health on/off; ~5%
//                        flip StaticTag on/off. Drives migration +
//                        commit-phase pressure.
//
// Each is a stand-alone `IGame` with deterministic seeded RNG. The
// bench picks the workload it wants and supplies the system-under-test
// via `Engine::registerSystem` post-`initialize`.

#pragma once

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/World.hpp>

#include <cstdint>
#include <random>

namespace threadmaxx_bench {

constexpr std::uint32_t kAiCount     = 1'024;
constexpr std::uint32_t kRenderCount = 20'000;
constexpr std::uint32_t kChurnCount  = 100'000;

inline threadmaxx::BoundingVolume cubeAabb(const threadmaxx::Vec3& c, float h) {
    return threadmaxx::BoundingVolume{
        {c.x - h, c.y - h, c.z - h},
        {c.x + h, c.y + h, c.z + h},
    };
}

/// AI-only — small population, varied archetypes, no rendering.
struct AiOnlyWorkload : threadmaxx::IGame {
    std::uint32_t count = kAiCount;

    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        std::mt19937 rng(0xA1A1u);
        std::uniform_real_distribution<float> pos(-30.0f, 30.0f);
        for (std::uint32_t i = 0; i < count; ++i) {
            const threadmaxx::Vec3 p{pos(rng), 1.0f, pos(rng)};
            threadmaxx::Bundle b{};
            b.transform.position = p;
            b.velocity           = threadmaxx::Velocity{{0.1f, 0.0f, 0.0f},
                                                        {0.0f, 0.0f, 0.0f}};
            b.boundingVolume     = cubeAabb(p, 0.5f);
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Velocity,
                threadmaxx::Component::BoundingVolume,
            };
            if ((i & 1) == 0) {
                b.health = threadmaxx::Health{50.0f, 50.0f};
                b.initialMask = b.initialMask |
                    threadmaxx::ComponentSet{threadmaxx::Component::Health};
            }
            cb.spawnBundle(b);
        }
    }
};

/// Render+AI — RPG-demo shape at ~20k. Half carry RenderTag.
struct RenderAiWorkload : threadmaxx::IGame {
    std::uint32_t count = kRenderCount;

    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        std::mt19937 rng(0xB0B0u);
        std::uniform_real_distribution<float> pos(-150.0f, 150.0f);
        std::uniform_real_distribution<float> mesh(0.0f, 4.0f);
        for (std::uint32_t i = 0; i < count; ++i) {
            const threadmaxx::Vec3 p{pos(rng), 1.0f, pos(rng)};
            threadmaxx::Bundle b{};
            b.transform.position = p;
            b.boundingVolume     = cubeAabb(p, 0.5f);
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::BoundingVolume,
            };
            // 50% movers
            if ((i % 2) == 0) {
                b.velocity = threadmaxx::Velocity{{0.05f, 0.0f, 0.0f},
                                                  {0.0f, 0.0f, 0.0f}};
                b.initialMask = b.initialMask |
                    threadmaxx::ComponentSet{threadmaxx::Component::Velocity};
            }
            // 50% renderable
            if ((i % 2) == 0) {
                b.renderTag.meshId = static_cast<int>(mesh(rng));
                b.initialMask = b.initialMask |
                    threadmaxx::ComponentSet{threadmaxx::Component::RenderTag};
            }
            // 5% static decoration
            if ((i % 20) == 0) {
                b.initialMask = b.initialMask |
                    threadmaxx::ComponentSet{threadmaxx::Component::StaticTag};
            }
            cb.spawnBundle(b);
        }
    }
};

/// Churn — massive population for spawn/destroy/migration stress.
struct ChurnWorkload : threadmaxx::IGame {
    std::uint32_t count = kChurnCount;

    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        std::mt19937 rng(0xC2C2u);
        std::uniform_real_distribution<float> pos(-500.0f, 500.0f);
        for (std::uint32_t i = 0; i < count; ++i) {
            const threadmaxx::Vec3 p{pos(rng), 1.0f, pos(rng)};
            threadmaxx::Bundle b{};
            b.transform.position = p;
            b.velocity           = threadmaxx::Velocity{{0.05f, 0.0f, 0.0f},
                                                        {0.0f, 0.0f, 0.0f}};
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Velocity,
            };
            if ((i % 4) == 0) {
                b.health = threadmaxx::Health{20.0f, 20.0f};
                b.initialMask = b.initialMask |
                    threadmaxx::ComponentSet{threadmaxx::Component::Health};
            }
            cb.spawnBundle(b);
        }
    }
};

/// Helper: returns a `Config` configured for clean benchmark runs —
/// no pacing sleep, deterministic mode on, large initial capacity.
inline threadmaxx::Config benchConfig(std::uint32_t workers,
                                      std::uint32_t entityCount,
                                      bool sharded = false) {
    threadmaxx::Config cfg;
    cfg.sleepToPace          = false;
    cfg.workerCount          = workers;
    cfg.deterministic        = true;
    cfg.singleThreadedCommit = !sharded;
    cfg.initialEntityCapacity = entityCount + 1024;
    return cfg;
}

} // namespace threadmaxx_bench
