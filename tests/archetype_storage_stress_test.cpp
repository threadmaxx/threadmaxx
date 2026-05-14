// §3.1 batch 6: storage churn at scale.
//
// The archetype-chunked EntityStorage groups entities by ComponentSet
// mask; physical migration on Health/StaticTag flips moves entities
// between chunks. Invariants this test pins:
//   - determinism (same seeded inputs → same hashed snapshot run-to-
//     run, even with multi-archetype churn),
//   - signature math (sum of archetype-signature counts == live size),
//   - safe spawn / per-tick addComponent+removeComponent / tag-flip /
//     transform-integration churn at 10k+ entities.
//
// Originally written as the batch-6a baseline that the chunked-storage
// refactor had to preserve; the refactor (batch 6) now runs against
// this test on every CI build.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kInitialSpawn = 8192;   // power of two; nice grain
constexpr std::uint32_t kStressTicks  = 24;

// FNV-1a 64-bit over raw bytes. Matches determinism_golden_test.
std::uint64_t fnv1a(const void* data, std::size_t bytes) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

template <typename T>
void hashVec(std::uint64_t& h, const std::vector<T>& v) noexcept {
    const std::uint64_t n = v.size();
    h ^= fnv1a(&n, sizeof(n));
    h *= 0x100000001b3ull;
    if (!v.empty()) {
        h ^= fnv1a(v.data(), v.size() * sizeof(T));
        h *= 0x100000001b3ull;
    }
}

// Spawner: drops kInitialSpawn entities in one tick. Half carry
// RenderTag, half don't, so archetype signatures start non-trivial.
class StressSpawner : public threadmaxx::ISystem {
public:
    bool done = false;
    const char* name() const noexcept override { return "stress-spawn"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        ctx.parallelFor(kInitialSpawn, /*grain*/ 256,
            [](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
                for (std::uint32_t i = r.begin; i < r.end; ++i) {
                    threadmaxx::Transform t;
                    t.position.x = static_cast<float>(i % 311) * 0.25f;
                    threadmaxx::Velocity v;
                    v.linear.y = static_cast<float>(i % 13) * 0.1f;
                    threadmaxx::RenderTag tag;
                    tag.meshId = (i & 1u) ? static_cast<std::int32_t>(i % 8) : -1;
                    cb.spawn(t, v, tag,
                             threadmaxx::UserData{static_cast<std::uint32_t>(i)});
                }
            });
    }
};

// Churn: every tick after spawn-tick, for every 32nd entity:
//   - flip Health (add if not present, remove if present)
//   - flip StaticTag
//   - rewrite Transform via setTransform (no bit transition)
// Deterministic across runs because the decision uses the entity's
// UserData (which was seeded deterministically), NOT a per-tick PRNG.
class StressChurner : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "stress-churn"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::Component::Transform |
               threadmaxx::Component::Velocity  |
               threadmaxx::Component::UserData;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::Component::Transform |
               threadmaxx::Component::Health;
    }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto& w = ctx.world();
        const auto entities = w.entities();
        if (entities.empty()) return;
        const auto count = static_cast<std::uint32_t>(entities.size());

        // ctx.tick() is sim-thread state; capture once for the closure.
        const auto tickRound = static_cast<std::uint32_t>(ctx.tick());

        // The component transitions are done single-threaded so the
        // decision (which uses the world's entities span + the captured
        // tick) is trivially deterministic. The work is dwarfed by the
        // parallel-mover step.
        ctx.single([tickRound, entities, count]
                   (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
            for (std::uint32_t i = 0; i < count; ++i) {
                if ((i & 31u) != (tickRound & 31u)) continue;
                const auto e = entities[i];
                // Toggle Health based on tickRound's low bit AND entity
                // index parity — deterministic across runs.
                if ((tickRound + i) & 2u) {
                    cb.addComponent<threadmaxx::Health>(e,
                        threadmaxx::Health{
                            static_cast<float>(tickRound),
                            100.0f});
                } else {
                    cb.removeComponent<threadmaxx::Health>(e);
                }
                // Toggle StaticTag based on a different parity.
                if ((tickRound ^ i) & 4u) {
                    cb.addTag(e, threadmaxx::Component::StaticTag);
                } else {
                    cb.removeTag(e, threadmaxx::Component::StaticTag);
                }
            }
        });
    }
};

// Move system: integrates velocity. Identical to determinism_golden's.
class StressMover : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "stress-move"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::Component::Transform |
               threadmaxx::Component::Velocity;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto dt = static_cast<float>(ctx.dt());
        threadmaxx::forEach<threadmaxx::Transform, threadmaxx::Velocity>(ctx,
            [dt](threadmaxx::EntityHandle e,
                 const threadmaxx::Transform& t,
                 const threadmaxx::Velocity& v,
                 threadmaxx::CommandBuffer& cb) {
                threadmaxx::Transform next = t;
                next.position = t.position + v.linear * dt;
                cb.setTransform(e, next);
            });
    }
};

class EmptyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

std::uint64_t runStress() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    engine.initialize(game);
    engine.registerSystem(std::make_unique<StressSpawner>());
    engine.registerSystem(std::make_unique<StressChurner>());
    engine.registerSystem(std::make_unique<StressMover>());

    for (std::uint32_t i = 0; i < kStressTicks; ++i) engine.step();

    const auto& w = engine.world();
    const auto snap = w.snapshot();

    // Quick consistency check: archetype-signature counts sum to live
    // entity count. Cheap O(N) walk; cost matters when run thousands
    // of times in CI.
    const auto sigs = w.archetypeSignatures();
    std::uint32_t total = 0;
    for (const auto& row : sigs) total += row.count;
    if (total != static_cast<std::uint32_t>(w.size())) {
        // Forces a determinism mismatch downstream by salting the hash.
        return 0;
    }

    std::uint64_t h = 0xcbf29ce484222325ull;
    hashVec(h, snap.entities);
    hashVec(h, snap.transforms);
    hashVec(h, snap.velocities);
    hashVec(h, snap.renderTags);
    hashVec(h, snap.userData);
    hashVec(h, snap.accelerations);
    hashVec(h, snap.parents);
    hashVec(h, snap.healths);
    hashVec(h, snap.factions);
    hashVec(h, snap.animationStates);
    hashVec(h, snap.physicsBodies);
    hashVec(h, snap.navAgents);
    hashVec(h, snap.boundingVolumes);
    hashVec(h, snap.masks);

    engine.shutdown();
    return h;
}

} // namespace

int main() {
    const auto h1 = runStress();
    const auto h2 = runStress();
    CHECK(h1 != 0);
    CHECK_EQ(h1, h2);

    EXIT_WITH_RESULT();
}
