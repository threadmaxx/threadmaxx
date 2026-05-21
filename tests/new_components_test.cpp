// §3.1 batch 5: 6 new POD components — Health, Faction, AnimationStateRef,
// PhysicsBodyRef, NavAgentRef, BoundingVolume.
//
// Each one must:
//   - Have a dense array on the world.
//   - Be settable via cb.set*; the corresponding presence bit gets attached.
//   - Round-trip through World::snapshot + serialize/deserialize.
//   - Compose into a Bundle and survive spawnBundle.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <sstream>

namespace {

class SeedingGame : public threadmaxx::IGame {
public:
    threadmaxx::EntityHandle handle{};
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        // Spawn one entity with the default mask (no batch-5 bits).
        seed.spawn(threadmaxx::Transform{});
    }
};

class AttachSystem : public threadmaxx::ISystem {
public:
    bool done = false;
    threadmaxx::EntityHandle target;
    const char* name() const noexcept override { return "attach"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        const auto& w = ctx.world();
        if (w.entities().empty()) return;
        target = w.entities()[0];
        ctx.single([target = target]
            (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
                cb.setHealth(target, threadmaxx::Health{75.0f, 100.0f});
                cb.setFaction(target, threadmaxx::Faction{7u});
                cb.setPhysicsBodyRef(target, threadmaxx::PhysicsBodyRef{0xABCDu});
                cb.setNavAgentRef(target, threadmaxx::NavAgentRef{0xDEEDu});
                threadmaxx::BoundingVolume bv;
                bv.min = {-1, -1, -1};
                bv.max = { 1,  1,  1};
                cb.setBoundingVolume(target, bv);
                threadmaxx::AnimationStateRef asr;
                asr.state = 3;
                asr.t     = 0.25f;
                cb.setAnimationStateRef(target, asr);
            });
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    SeedingGame game;
    CHECK(engine.initialize(game));
    auto* sys = new AttachSystem();
    engine.registerSystem(std::unique_ptr<ISystem>(sys));
    engine.step();

    const auto& w = engine.world();
    CHECK_EQ(w.size(), std::size_t{1});
    const EntityHandle e = w.entities()[0];

    // Presence bits all attached after set*.
    CHECK(w.has<Health>(e));
    CHECK(w.has<Faction>(e));
    CHECK(w.has<AnimationStateRef>(e));
    CHECK(w.has<PhysicsBodyRef>(e));
    CHECK(w.has<NavAgentRef>(e));
    CHECK(w.has<BoundingVolume>(e));

    // Dense values match what we wrote.
    CHECK_EQ(w.get<Health>(e).current, 75.0f);
    CHECK_EQ(w.get<Health>(e).max,     100.0f);
    CHECK_EQ(w.get<Faction>(e).id,     7u);
    CHECK_EQ(w.get<PhysicsBodyRef>(e).handle, std::uint64_t{0xABCDu});
    CHECK_EQ(w.get<NavAgentRef>(e).handle,    std::uint64_t{0xDEEDu});
    CHECK_EQ(w.get<AnimationStateRef>(e).state, 3u);
    CHECK_EQ(w.get<AnimationStateRef>(e).t,     0.25f);
    CHECK_EQ(w.get<BoundingVolume>(e).min.x, -1.0f);
    CHECK_EQ(w.get<BoundingVolume>(e).max.z,  1.0f);

    // Snapshot + serialize round-trip preserves all the new arrays.
    const auto snap = w.snapshot();
    CHECK_EQ(snap.healths.size(),         std::size_t{1});
    CHECK_EQ(snap.factions.size(),        std::size_t{1});
    CHECK_EQ(snap.animationStates.size(), std::size_t{1});
    CHECK_EQ(snap.physicsBodies.size(),   std::size_t{1});
    CHECK_EQ(snap.navAgents.size(),       std::size_t{1});
    CHECK_EQ(snap.boundingVolumes.size(), std::size_t{1});
    CHECK_EQ(snap.healths[0].max,         100.0f);
    CHECK_EQ(snap.factions[0].id,         7u);

    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    serialize(ss, snap);
    WorldSnapshot back;
    CHECK(deserialize(ss, back));
    CHECK_EQ(back.healths.size(),         std::size_t{1});
    CHECK_EQ(back.healths[0].current,     75.0f);
    CHECK_EQ(back.factions[0].id,         7u);
    CHECK_EQ(back.physicsBodies[0].handle, std::uint64_t{0xABCDu});
    CHECK_EQ(back.boundingVolumes[0].min.x, -1.0f);
    // The high-bit mask roundtrips: every batch-5 bit set above must
    // survive (Health=7, Faction=8, AnimationStateRef=9, ...).
    CHECK(back.masks[0].has(Component::Health));
    CHECK(back.masks[0].has(Component::BoundingVolume));

    // Bundle path: spawn with bundle(...) attaches exactly the listed
    // bits and nothing else. A bundle of (Transform, Health, Faction)
    // does NOT carry Velocity or UserData.
    auto b = bundle(Transform{}, Health{1.0f, 1.0f}, Faction{99u});
    EntityHandle bundled;
    {
        class Once : public ISystem {
        public:
            Bundle b;
            EntityHandle* out;
            bool done = false;
            const char* name() const noexcept override { return "once"; }
            void update(SystemContext& ctx) override {
                if (done) return;
                done = true;
                EntityHandle reserved = ctx.reserveHandle();
                *out = reserved;
                ctx.single([reserved, bb = b]
                    (Range, CommandBuffer& cb) {
                        cb.spawnBundle(reserved, bb);
                    });
            }
        };
        auto u = std::make_unique<Once>();
        u->b = b;
        u->out = &bundled;
        engine.registerSystem(std::move(u));
        engine.step();
    }
    CHECK(w.alive(bundled));
    CHECK(w.has<Transform>(bundled));
    CHECK(w.has<Health>(bundled));
    CHECK(w.has<Faction>(bundled));
    CHECK(!w.has<Velocity>(bundled));
    CHECK(!w.has<UserData>(bundled));
    CHECK_EQ(w.get<Faction>(bundled).id, 99u);

    // §3.1 batch 5 — per-handle accessors return the dense value on a
    // handle that owns the bit, nullptr on one that doesn't.
    {
        const auto* a = w.tryGetAnimationStateRef(e);
        const auto* p = w.tryGetPhysicsBodyRef(e);
        const auto* n = w.tryGetNavAgentRef(e);
        const auto* b2 = w.tryGetBoundingVolume(e);
        CHECK(a && p && n && b2);
        CHECK_EQ(a->state,  3u);
        CHECK_EQ(p->handle, std::uint64_t{0xABCDu});
        CHECK_EQ(n->handle, std::uint64_t{0xDEEDu});
        CHECK_EQ(b2->max.z, 1.0f);

        // `bundled` only has Transform/Health/Faction — the batch-5 ref
        // accessors must return nullptr.
        CHECK(w.tryGetAnimationStateRef(bundled) == nullptr);
        CHECK(w.tryGetPhysicsBodyRef(bundled)    == nullptr);
        CHECK(w.tryGetNavAgentRef(bundled)       == nullptr);
        CHECK(w.tryGetBoundingVolume(bundled)    == nullptr);
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
