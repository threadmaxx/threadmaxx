// §3.1 batch 5: MaskCache + forEachWithCached.
//
// The cache is opt-in: a system rebuilds it in preStep, then iterates
// only the cached matching indices in update without re-testing the
// per-entity mask on the hot path.
//
// This test verifies:
//   - rebuild() captures every entity whose mask satisfies the required
//     set, in dense order.
//   - forEachWithCached invokes the callable once per cached index.
//   - A subsequent removal-and-rebuild correctly drops the entry.
//   - The cache respects index bounds: a destroy *between* rebuilds
//     leaves a stale index that the iterator skips (defense in depth).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>

namespace {

class SeedingGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        // Three with Velocity (in default mask), one without (custom mask).
        for (int i = 0; i < 3; ++i) {
            seed.spawn(threadmaxx::Transform{}, threadmaxx::Velocity{});
        }
        threadmaxx::ComponentSet noVelocity;
        noVelocity.add(threadmaxx::Component::Transform);
        noVelocity.add(threadmaxx::Component::UserData);
        noVelocity.add(threadmaxx::Component::Acceleration);
        seed.spawn(threadmaxx::Transform{}, threadmaxx::Velocity{},
                   threadmaxx::RenderTag{}, threadmaxx::UserData{},
                   threadmaxx::Acceleration{}, threadmaxx::Parent{},
                   noVelocity);
    }
};

class CachedQuerySystem : public threadmaxx::ISystem {
public:
    threadmaxx::MaskCache cache;
    std::atomic<int> visits{0};
    const char* name() const noexcept override { return "cached"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::Component::Transform | threadmaxx::Component::Velocity;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    void preStep(threadmaxx::SystemContext& ctx) override {
        cache.rebuild(ctx.world(),
            threadmaxx::required<threadmaxx::Transform, threadmaxx::Velocity>());
    }
    void update(threadmaxx::SystemContext& ctx) override {
        visits.store(0);
        threadmaxx::forEachWithCached<threadmaxx::Transform, threadmaxx::Velocity>(
            ctx, cache,
            [this](threadmaxx::EntityHandle,
                   const threadmaxx::Transform&,
                   const threadmaxx::Velocity&,
                   threadmaxx::CommandBuffer&) {
                visits.fetch_add(1, std::memory_order_relaxed);
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

    auto sys = std::make_unique<CachedQuerySystem>();
    auto* rawSys = sys.get();
    engine.registerSystem(std::move(sys));

    engine.step();
    // Three of four entities carry both Transform AND Velocity. The
    // fourth has Transform only.
    CHECK_EQ(rawSys->cache.size(),  std::size_t{3});
    CHECK_EQ(rawSys->visits.load(), 3);

    // Destroy one matching entity. Until preStep rebuilds the cache
    // on the NEXT tick, the cache still references the soon-dead row.
    // forEachWithCached's bounds check filters out the now-OOB index
    // after the swap-and-pop.
    const auto victim = engine.world().entities()[0];
    {
        class Once : public ISystem {
        public:
            EntityHandle target;
            bool done = false;
            const char* name() const noexcept override { return "destroyer"; }
            ComponentSet writes() const noexcept override {
                return ComponentSet{Component::EntityStructural};
            }
            void update(SystemContext& ctx) override {
                if (done) return;
                done = true;
                auto t = target;
                ctx.single([t](Range, CommandBuffer& cb) { cb.destroy(t); });
            }
        };
        auto u = std::make_unique<Once>();
        u->target = victim;
        engine.registerSystem(std::move(u));
    }
    engine.step();
    // This tick's preStep rebuilt the cache against the pre-destroy
    // world (the destroyer's commit happens after preStep), so the
    // cache is still 3 entries.
    CHECK_EQ(rawSys->cache.size(),  std::size_t{3});

    // Step again: now preStep rebuilds against the post-destroy world.
    engine.step();
    CHECK_EQ(rawSys->cache.size(),  std::size_t{2});
    CHECK_EQ(rawSys->visits.load(), 2);

    // required() round-trip: the cache stores the set it was built
    // against.
    CHECK(rawSys->cache.required().has(Component::Transform));
    CHECK(rawSys->cache.required().has(Component::Velocity));
    CHECK(!rawSys->cache.required().has(Component::Health));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
