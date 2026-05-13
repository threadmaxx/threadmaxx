// World::has<T> / World::get<T>: header-only sugar over the per-entity
// component mask + dense accessors. Covers each built-in component, both
// hits and misses, and confirms the mask is what's consulted (a present-
// in-storage but bit-cleared component reads as has<T>() == false).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

struct HasGetGame : threadmaxx::IGame {
    threadmaxx::EntityHandle renderable;
    threadmaxx::EntityHandle bare;
    threadmaxx::EntityHandle child;

    void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        using namespace threadmaxx;
        renderable = e.reserveEntityHandle();
        bare       = e.reserveEntityHandle();
        child      = e.reserveEntityHandle();

        const ComponentSet kBaseMask =
            ComponentSet{Component::Transform}
            | ComponentSet{Component::Velocity}
            | ComponentSet{Component::UserData}
            | ComponentSet{Component::Acceleration};

        // Renderable: base mask + RenderTag bit.
        seed.spawn(renderable,
                   Transform{}, Velocity{}, RenderTag{7}, UserData{42},
                   Acceleration{}, Parent{},
                   kBaseMask | ComponentSet{Component::RenderTag});

        // Bare: just the base mask. No RenderTag, no Parent bit.
        seed.spawn(bare,
                   Transform{}, Velocity{}, RenderTag{}, UserData{},
                   Acceleration{}, Parent{},
                   kBaseMask);

        // Child: base mask + Parent bit attached to renderable.
        seed.spawn(child,
                   Transform{}, Velocity{}, RenderTag{}, UserData{},
                   Acceleration{}, Parent{renderable, Transform{}},
                   kBaseMask | ComponentSet{Component::Parent});
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;

    Engine engine(cfg);
    HasGetGame game;
    CHECK(engine.initialize(game));

    const auto& world = engine.world();
    CHECK_EQ(world.size(), std::size_t{3});

    // has<T> hits for the universal components.
    CHECK(world.has<Transform>(game.renderable));
    CHECK(world.has<Velocity>(game.renderable));
    CHECK(world.has<UserData>(game.renderable));
    CHECK(world.has<Acceleration>(game.renderable));
    CHECK(world.has<RenderTag>(game.renderable));
    CHECK(!world.has<Parent>(game.renderable));

    CHECK(world.has<Transform>(game.bare));
    CHECK(!world.has<RenderTag>(game.bare));
    CHECK(!world.has<Parent>(game.bare));

    CHECK(world.has<Parent>(game.child));
    CHECK(!world.has<RenderTag>(game.child));

    // get<T> returns the same value tryGet returns. Use UserData value
    // since RenderTag has a sentinel-y default.
    CHECK_EQ(world.get<UserData>(game.renderable).value, std::uint64_t{42});
    CHECK_EQ(world.get<RenderTag>(game.renderable).meshId, 7);

    // Parent's parent pointer round-trips.
    const auto& p = world.get<Parent>(game.child);
    CHECK_EQ(p.parent, game.renderable);

    // Stale handle reads false through has<T>.
    EntityHandle stale = game.renderable;
    stale.generation = 0;
    CHECK(!world.has<Transform>(stale));
    CHECK(!world.has<RenderTag>(stale));

    // Default-constructed handle: invalid, has<T> reads false uniformly.
    EntityHandle invalid;
    CHECK(!world.has<Transform>(invalid));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
