// §3.6 small wins: Query.hpp covers Acceleration + Parent, EngineStats has
// commitDurationSeconds, JobSystemStats reports own-pops and steals.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>

namespace {

// Verifies forEach<Acceleration> and forEach<Parent> compile and visit
// every entity.
class AccelerationCounter : public threadmaxx::ISystem {
public:
    std::atomic<std::uint32_t> count{0};
    const char* name() const noexcept override { return "accel_counter"; }
    void update(threadmaxx::SystemContext& ctx) override {
        threadmaxx::forEach<threadmaxx::Acceleration>(ctx,
            [this](threadmaxx::EntityHandle,
                   const threadmaxx::Acceleration&,
                   threadmaxx::CommandBuffer&) {
                count.fetch_add(1, std::memory_order_relaxed);
            });
    }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Acceleration};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
};

class ParentCounter : public threadmaxx::ISystem {
public:
    std::atomic<std::uint32_t> count{0};
    const char* name() const noexcept override { return "parent_counter"; }
    void update(threadmaxx::SystemContext& ctx) override {
        threadmaxx::forEachWith<threadmaxx::Parent>(ctx,
            [this](threadmaxx::EntityHandle,
                   const threadmaxx::Parent&,
                   threadmaxx::CommandBuffer&) {
                count.fetch_add(1, std::memory_order_relaxed);
            });
    }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Parent};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: query helpers compile for Acceleration and Parent.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine e(cfg);
        struct G : IGame {
            AccelerationCounter* accel = nullptr;
            ParentCounter*       par   = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer& seed) override {
                auto a = std::make_unique<AccelerationCounter>();
                accel = a.get();
                eng.registerSystem(std::move(a));
                auto p = std::make_unique<ParentCounter>();
                par = p.get();
                eng.registerSystem(std::move(p));

                // Spawn 3 entities, one with a parent.
                auto h0 = eng.reserveEntityHandle();
                auto h1 = eng.reserveEntityHandle();
                auto h2 = eng.reserveEntityHandle();
                seed.spawn(h0, Transform{});
                seed.spawn(h1, Transform{});
                seed.spawn(h2, Transform{}, {}, {}, {}, {},
                           Parent{h0, Transform{}},
                           ComponentSet{Component::Transform}
                             | ComponentSet{Component::Velocity}
                             | ComponentSet{Component::UserData}
                             | ComponentSet{Component::Acceleration}
                             | ComponentSet{Component::Parent});
            }
        } g;
        CHECK(e.initialize(g));
        e.step();
        // forEach<Acceleration> visits every live entity (acceleration is
        // present on all of them by default).
        CHECK_EQ(g.accel->count.load(), std::uint32_t{3});
        // forEachWith<Parent> only visits entities whose mask has Parent.
        CHECK_EQ(g.par->count.load(), std::uint32_t{1});
        e.shutdown();
    }

    // Test 2: EngineStats.commitDurationSeconds is non-negative and
    // bounded by the total step time.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine e(cfg);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer& seed) override {
                for (int i = 0; i < 10; ++i) seed.spawn(Transform{});
            }
        } g;
        CHECK(e.initialize(g));
        e.step();
        const auto s = e.stats();
        CHECK(s.commitDurationSeconds >= 0.0);
        // Commit happens inside step(), so it must not exceed the
        // total step time.
        CHECK(s.commitDurationSeconds <= s.lastStepSeconds + 1e-6);
        e.shutdown();
    }

    // Test 3: JobSystemStats reports totals matching engine-submitted jobs.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine e(cfg);
        struct DriverSystem : ISystem {
            const char* name() const noexcept override { return "driver"; }
            void update(SystemContext& ctx) override {
                ctx.parallelFor(/*count*/ 8, /*grain*/ 1,
                    [](Range, CommandBuffer&) {});
            }
            ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
            ComponentSet writes() const noexcept override { return ComponentSet::none(); }
        };
        struct G : IGame {
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                eng.registerSystem(std::make_unique<DriverSystem>());
            }
        } g;
        CHECK(e.initialize(g));
        e.step();
        const auto js = e.jobSystemStats();
        CHECK_EQ(js.totalJobs, std::uint64_t{8});
        CHECK_EQ(js.ownPops + js.stolenJobs, js.totalJobs);
        CHECK(js.workerCount >= 1u);
        e.shutdown();
    }

    EXIT_WITH_RESULT();
}
