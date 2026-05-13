// Bundle / spawnBundle (§3.5). Verifies that:
//   - bundle(Cs...) yields the union of component bits in its initialMask
//   - cb.spawnBundle(b) writes the bundle's values + mask through to the
//     materialized entity
//   - cb.spawnBundle(handle, b) materializes a previously reserved slot
//   - duplicate type in the pack overwrites silently and still flips the bit

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

struct BundleGame : threadmaxx::IGame {
    threadmaxx::EntityHandle fresh;
    threadmaxx::EntityHandle reserved;
    threadmaxx::EntityHandle minimal;

    void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        using namespace threadmaxx;

        // Fresh slot: full enemy bundle.
        const auto enemyBundle = bundle(
            Transform{}, Velocity{}, RenderTag{/*meshId*/ 7},
            UserData{/*value*/ 123}, Acceleration{});
        seed.spawnBundle(enemyBundle);

        // Pre-reserved slot.
        reserved = e.reserveEntityHandle();
        const auto childBundle = bundle(
            Transform{}, Parent{reserved, Transform{}});
        // child's parent points at the renderable we'll grab below.
        seed.spawnBundle(reserved, bundle(
            Transform{}, RenderTag{99}, UserData{555}));
        // Spawn the child with Parent attached.
        seed.spawnBundle(childBundle);

        // Minimal: just a Transform — no Velocity, UserData, etc.
        seed.spawnBundle(bundle(Transform{}));
        (void)fresh;  // assigned via post-commit lookup below
        (void)minimal;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;

    Engine engine(cfg);
    BundleGame game;
    CHECK(engine.initialize(game));

    const auto& world = engine.world();
    CHECK_EQ(world.size(), std::size_t{4});

    // The reserved entity should be alive with the expected components.
    CHECK(world.alive(game.reserved));
    CHECK(world.has<Transform>(game.reserved));
    CHECK(world.has<RenderTag>(game.reserved));
    CHECK(world.has<UserData>(game.reserved));
    CHECK(!world.has<Velocity>(game.reserved));
    CHECK(!world.has<Parent>(game.reserved));
    CHECK_EQ(world.get<RenderTag>(game.reserved).meshId, 99);
    CHECK_EQ(world.get<UserData>(game.reserved).value, std::uint64_t{555});

    // Find the minimal entity (only Transform present).
    int minimalCount = 0;
    int enemyCount = 0;
    int childCount = 0;
    for (auto e : world.entities()) {
        const auto* m = world.tryGetComponentMask(e);
        CHECK(m != nullptr);
        // Minimal has just Transform.
        if (m->bits() == static_cast<std::uint32_t>(Component::Transform)) {
            ++minimalCount;
        }
        // Enemy: Transform+Velocity+RenderTag+UserData+Acceleration.
        const ComponentSet enemyMask =
            ComponentSet{Component::Transform}
            | ComponentSet{Component::Velocity}
            | ComponentSet{Component::RenderTag}
            | ComponentSet{Component::UserData}
            | ComponentSet{Component::Acceleration};
        if (*m == enemyMask) {
            ++enemyCount;
            CHECK_EQ(world.get<RenderTag>(e).meshId, 7);
            CHECK_EQ(world.get<UserData>(e).value, std::uint64_t{123});
        }
        // Child: Transform+Parent.
        if (*m == (ComponentSet{Component::Transform}
                   | ComponentSet{Component::Parent})) {
            ++childCount;
            CHECK_EQ(world.get<Parent>(e).parent, game.reserved);
        }
    }
    CHECK_EQ(minimalCount, 1);
    CHECK_EQ(enemyCount, 1);
    CHECK_EQ(childCount, 1);

    // Duplicate type in the pack: second value wins, mask still has the bit.
    const auto dup = bundle(UserData{1}, UserData{2});
    CHECK_EQ(dup.userData.value, std::uint64_t{2});
    CHECK(dup.initialMask.has(Component::UserData));
    // No other bits flipped.
    CHECK_EQ(dup.initialMask.bits(),
             static_cast<std::uint32_t>(Component::UserData));

    // Empty bundle: empty mask.
    const auto empty = bundle();
    CHECK(empty.initialMask.empty());

    engine.shutdown();
    EXIT_WITH_RESULT();
}
