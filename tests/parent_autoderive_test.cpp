// §3.6c: the default-mask spawn overloads now auto-derive the Parent
// presence bit from the supplied Parent's parent.valid(). Mirrors the
// existing RenderTag auto-derive behavior.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

struct Game : threadmaxx::IGame {
    threadmaxx::EntityHandle root;
    threadmaxx::EntityHandle childWithParent;
    threadmaxx::EntityHandle childWithoutParent;

    void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        using namespace threadmaxx;
        root              = e.reserveEntityHandle();
        childWithParent   = e.reserveEntityHandle();
        childWithoutParent = e.reserveEntityHandle();

        // Root: 6-arg default-mask overload, no parent (kInvalidEntity).
        seed.spawn(root, Transform{});

        // Child with a valid parent — Parent bit should be auto-derived ON.
        seed.spawn(childWithParent, Transform{}, Velocity{}, RenderTag{},
                   UserData{}, Acceleration{},
                   Parent{root, Transform{}});

        // Child with an invalid parent — Parent bit should stay OFF.
        seed.spawn(childWithoutParent, Transform{}, Velocity{}, RenderTag{},
                   UserData{}, Acceleration{}, Parent{});
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;

    Engine engine(cfg);
    Game game;
    CHECK(engine.initialize(game));

    const auto& world = engine.world();

    CHECK(world.has<Parent>(game.childWithParent));
    CHECK(!world.has<Parent>(game.childWithoutParent));
    CHECK(!world.has<Parent>(game.root));

    // The actual Parent value round-trips even when the bit is off.
    const auto& p = world.get<Parent>(game.childWithParent);
    CHECK_EQ(p.parent, game.root);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
