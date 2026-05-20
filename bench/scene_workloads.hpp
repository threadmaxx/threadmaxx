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

/// §3.10.4 batch 26 — RPG-stress workload sizing. Mirrors
/// `examples/rpg_demo` with `--stress`: ~100k NPCs + ~5k pickups +
/// terrain + player + sword. Configurable via the workload's public
/// members so a single bench can sweep across multiple scales (10k →
/// 100k+ NPCs).
constexpr std::uint32_t kRpgStressNpcCount     = 100'000;
constexpr std::uint32_t kRpgStressPickupCount  = 5'000;
constexpr std::uint32_t kRpgStressTerrainCount = 0; // single big terrain

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

/// §3.10.4 batch 26 — RPG-shaped stress workload.
///
/// Mirrors `examples/rpg_demo` with `--stress` at the engine level:
/// no Vulkan, no GLFW, no user components — five distinct archetype
/// shapes built from built-in components only, so the bench is
/// reproducible on any host that builds the core library.
///
/// Archetype mix (after a clean `onSetup`):
///   - Player    (1)        Transform + Velocity + Faction + BoundingVolume + Health
///   - Sword     (1)        Transform + Parent   + BoundingVolume   (child of player)
///   - Terrain   (1)        Transform + Faction  + BoundingVolume + StaticTag
///   - NPC       (npcCount) Transform + Velocity + Faction + BoundingVolume + Health
///   - Pickup    (pickupCount) Transform + Faction + BoundingVolume
///
/// At default scaling that's 5 chunks with row counts
/// {1, 1, 1, 100k, 5k} — directly matches the post-`--stress` archetype
/// distribution `rpg_demo` produces, which is the workload the
/// post-renderer-fix profile (2026-05-20) flagged as the new shape for
/// engine-side optimization.
///
/// Default counts are scaled down to 10k / 5k so a 60-bench run
/// completes in a reasonable amount of time; bump via the public
/// fields when running standalone.
struct RpgStressWorkload : threadmaxx::IGame {
    std::uint32_t npcCount    = 10'000;
    std::uint32_t pickupCount = 5'000;

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        std::mt19937 rng(0xD1D1u);
        std::uniform_real_distribution<float> pos(-30.0f, 30.0f);

        // Player
        const auto player = engine.reserveEntityHandle();
        {
            threadmaxx::Bundle b{};
            b.transform.position = {0.0f, 1.0f, 0.0f};
            b.velocity           = threadmaxx::Velocity{{0.0f, 0.0f, 0.0f},
                                                        {0.0f, 0.0f, 0.0f}};
            b.faction.id         = 0u;
            b.boundingVolume     = cubeAabb({0.0f, 1.0f, 0.0f}, 0.9f);
            b.health             = threadmaxx::Health{100.0f, 100.0f};
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Velocity,
                threadmaxx::Component::Faction,
                threadmaxx::Component::BoundingVolume,
                threadmaxx::Component::Health,
            };
            cb.spawnBundle(player, b);
        }

        // Sword (child of player)
        {
            const auto sword = engine.reserveEntityHandle();
            threadmaxx::Parent p;
            p.parent               = player;
            p.localOffset.position = {0.5f, 0.8f, -0.8f};
            p.localOffset.scale    = {0.18f, 0.18f, 1.4f};
            threadmaxx::Bundle b{};
            b.transform.position = {0.5f, 1.8f, -0.8f};
            b.parent             = p;
            b.boundingVolume     = cubeAabb({0.0f, 0.0f, 0.0f}, 0.3f);
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Parent,
                threadmaxx::Component::BoundingVolume,
            };
            cb.spawnBundle(sword, b);
        }

        // Terrain (one big static cube)
        {
            const auto terrain = engine.reserveEntityHandle();
            threadmaxx::Bundle b{};
            b.transform.position = {0.0f, -0.5f, 0.0f};
            b.transform.scale    = {60.0f, 0.2f, 60.0f};
            b.faction.id         = 0u;
            b.boundingVolume     = cubeAabb({0.0f, -0.5f, 0.0f}, 30.0f);
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Faction,
                threadmaxx::Component::BoundingVolume,
                threadmaxx::Component::StaticTag,
            };
            cb.spawnBundle(terrain, b);
        }

        // NPCs
        for (std::uint32_t i = 0; i < npcCount; ++i) {
            const threadmaxx::Vec3 p{pos(rng), 1.0f, pos(rng)};
            threadmaxx::Bundle b{};
            b.transform.position = p;
            b.velocity = threadmaxx::Velocity{{0.05f, 0.0f, 0.0f},
                                              {0.0f, 0.0f, 0.0f}};
            b.faction.id     = (i & 1u) ? 1u : 2u;
            b.boundingVolume = cubeAabb(p, 0.8f);
            b.health         = threadmaxx::Health{60.0f, 60.0f};
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Velocity,
                threadmaxx::Component::Faction,
                threadmaxx::Component::BoundingVolume,
                threadmaxx::Component::Health,
            };
            cb.spawnBundle(b);
        }

        // Pickups
        for (std::uint32_t i = 0; i < pickupCount; ++i) {
            const threadmaxx::Vec3 p{pos(rng), 0.4f, pos(rng)};
            threadmaxx::Bundle b{};
            b.transform.position = p;
            b.faction.id         = 0u;
            b.boundingVolume     = cubeAabb(p, 0.4f);
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Faction,
                threadmaxx::Component::BoundingVolume,
            };
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
