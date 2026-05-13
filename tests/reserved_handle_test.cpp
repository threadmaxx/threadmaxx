// §3.5 Reserved spawn handles: reserveHandle returns a valid handle,
// spawn(reserved, ...) materializes it, unconsumed reservations get reaped
// at step end, and parent/child can be spawned in one job.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

// A system that reserves two handles in update() and spawns a parent +
// child using them, all from one ctx.single() block. Captures the handles
// for external verification.
class ParentChildSpawner : public threadmaxx::ISystem {
public:
    bool fired = false;
    threadmaxx::EntityHandle parent{};
    threadmaxx::EntityHandle child{};

    const char* name() const noexcept override { return "spawn_pc"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (fired) return;
        fired = true;

        parent = ctx.reserveHandle();
        child  = ctx.reserveHandle();
        const auto p = parent;
        const auto c = child;
        ctx.single([p, c](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
            cb.spawn(p, threadmaxx::Transform{threadmaxx::Vec3{5, 0, 0}, {}, {1, 1, 1}});
            cb.spawn(c, threadmaxx::Transform{}, {}, {}, {}, {},
                     threadmaxx::Parent{p, threadmaxx::Transform{
                         threadmaxx::Vec3{0, 2, 0}, {}, {1, 1, 1}}},
                     threadmaxx::ComponentSet{threadmaxx::Component::Transform}
                       | threadmaxx::ComponentSet{threadmaxx::Component::Velocity}
                       | threadmaxx::ComponentSet{threadmaxx::Component::UserData}
                       | threadmaxx::ComponentSet{threadmaxx::Component::Acceleration}
                       | threadmaxx::ComponentSet{threadmaxx::Component::Parent});
        });
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: a reserveHandle during onSetup gets materialized cleanly.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine e(cfg);

        EntityHandle out{};
        struct G : IGame {
            EntityHandle* outHandle;
            void onSetup(Engine& eng, World&, CommandBuffer& seed) override {
                *outHandle = eng.reserveEntityHandle();
                seed.spawn(*outHandle, Transform{Vec3{1, 2, 3}, {}, {1, 1, 1}});
            }
        } g; g.outHandle = &out;
        CHECK(e.initialize(g));
        CHECK(out.valid());
        // After commit, the reservation is materialized; the world has one entity.
        CHECK_EQ(e.world().size(), std::size_t{1});
        const auto* t = e.world().tryGetTransform(out);
        CHECK(t != nullptr);
        CHECK_EQ(t->position.x, 1.0f);
        e.shutdown();
    }

    // Test 2: reservation made inside a system body materializes via
    // commit at the end of the wave. Parent/child spawned together.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine e(cfg);
        struct G : IGame {
            ParentChildSpawner* p = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                auto s = std::make_unique<ParentChildSpawner>();
                p = s.get();
                eng.registerSystem(std::move(s));
                eng.registerSystem(makeHierarchySystem());
            }
        } g;
        CHECK(e.initialize(g));

        // Pre-step the world is empty (no seed spawns).
        CHECK_EQ(e.world().size(), std::size_t{0});

        e.step();

        // Both reservations were consumed; world has 2 entities.
        CHECK_EQ(e.world().size(), std::size_t{2});
        CHECK(g.p->parent.valid());
        CHECK(g.p->child.valid());
        CHECK(e.world().tryGetTransform(g.p->parent) != nullptr);
        CHECK(e.world().tryGetTransform(g.p->child)  != nullptr);

        // Step once more so HierarchySystem propagates.
        e.step();
        const auto* cw = e.world().tryGetTransform(g.p->child);
        CHECK(cw != nullptr);
        // parent at (5,0,0), child local (0,2,0) → world (5,2,0).
        CHECK_EQ(cw->position.x, 5.0f);
        CHECK_EQ(cw->position.y, 2.0f);

        e.shutdown();
    }

    // Test 3: a reservation that's never matched by a spawn gets reaped.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine e(cfg);
        struct G : IGame {
            EntityHandle* outHandle;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                // Reserve but don't spawn.
                *outHandle = eng.reserveEntityHandle();
            }
        } g;
        EntityHandle leaked{};
        g.outHandle = &leaked;
        CHECK(e.initialize(g));
        CHECK(leaked.valid());
        // No materialization yet: world is empty.
        CHECK_EQ(e.world().size(), std::size_t{0});
        // After one step, the unconsumed reservation is reaped; the
        // outstanding handle no longer resolves to anything in the world.
        e.step();
        CHECK_EQ(e.world().size(), std::size_t{0});
        CHECK(e.world().tryGetTransform(leaked) == nullptr);
        e.shutdown();
    }

    EXIT_WITH_RESULT();
}
