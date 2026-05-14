// §3.1 batch 5: N-tick determinism golden-output test.
//
// Two engines, same seeded inputs, same fixed-step config, same system
// graph — after N ticks the world state (positions, velocities, masks)
// must hash identically. The hash is FNV-1a over a snapshot's POD
// bytes; if a worker-execution-order non-determinism crept in, the
// hashes diverge.
//
// This is the cheap regression guard the archetype refactor (§3.1
// batch 6, shipped 2026-05-14) was tested against — if a future
// change makes commit order race-sensitive, this test fails on the
// very first run of `cmake --build && ctest`.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// FNV-1a 64-bit over raw bytes. Deterministic, no STL hash quirks.
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

// One-shot spawner: 32 entities seeded with deterministic state on
// tick 0. Each carries Transform, Velocity, RenderTag, UserData; the
// movement system below integrates them.
class SpawnerSystem : public threadmaxx::ISystem {
public:
    bool done = false;
    const char* name() const noexcept override { return "spawner"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        ctx.parallelFor(32, /*grain*/ 1,
            [](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
                threadmaxx::Transform t;
                t.position.x = static_cast<float>(r.begin) * 0.5f;
                t.position.y = static_cast<float>(r.begin);
                threadmaxx::Velocity v;
                v.linear.x = static_cast<float>(r.begin % 7) * 0.1f;
                v.linear.y = static_cast<float>(r.begin % 3) * 0.2f;
                threadmaxx::RenderTag tag;
                tag.meshId = static_cast<std::int32_t>(r.begin);
                cb.spawn(t, v, tag, threadmaxx::UserData{r.begin});
            });
    }
};

class MoveSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "move"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::Component::Transform | threadmaxx::Component::Velocity;
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

std::uint64_t runN(std::uint32_t ticks) {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    engine.initialize(game);
    engine.registerSystem(std::make_unique<SpawnerSystem>());
    engine.registerSystem(std::make_unique<MoveSystem>());
    for (std::uint32_t i = 0; i < ticks; ++i) engine.step();

    const auto snap = engine.world().snapshot();
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
    // Two independent runs of the same scenario must produce the same
    // hash. The point isn't a stored magic constant (which would
    // depend on float quirks); it's that "run twice → same answer".
    const auto h1 = runN(64);
    const auto h2 = runN(64);
    CHECK_EQ(h1, h2);

    // Compare to a shorter run — different tick count must produce a
    // different hash (otherwise the system isn't actually evolving
    // state).
    const auto h3 = runN(8);
    CHECK(h1 != h3);

    EXIT_WITH_RESULT();
}
