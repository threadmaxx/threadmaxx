// §3.6b reserveEntityHandles / SystemContext::reserveHandles batch form.
// Verifies:
//   - Engine::reserveEntityHandles fills the span with `count` distinct
//     valid handles
//   - The handles materialize via cb.spawn(handle, ...) like one-shot
//     reservations do
//   - SystemContext::reserveHandles works from inside a system update

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <array>
#include <atomic>

namespace {

struct BatchGame : threadmaxx::IGame {
    std::array<threadmaxx::EntityHandle, 8> seeded{};

    void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        using namespace threadmaxx;
        const auto n = e.reserveEntityHandles(8, seeded);
        CHECK_EQ(n, std::uint32_t{8});
        for (auto h : seeded) {
            CHECK(h.valid());
            seed.spawn(h, Transform{}, Velocity{}, RenderTag{1});
        }
    }
};

// A system that reserves a batch of handles in update() and spawns
// children of a given parent into them.
class BatchSpawnerDirect : public threadmaxx::ISystem {
public:
    threadmaxx::EntityHandle parent;
    std::atomic<std::uint32_t> spawned{0};
    bool fired = false;

    const char* name() const noexcept override { return "batch-spawner-direct"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::all(); }

    void update(threadmaxx::SystemContext& ctx) override {
        if (fired) return;
        fired = true;
        std::array<threadmaxx::EntityHandle, 4> children{};
        const auto n = ctx.reserveHandles(4, children);
        spawned.store(n, std::memory_order_relaxed);
        auto p = parent;
        ctx.single([p, children](threadmaxx::Range,
                                 threadmaxx::CommandBuffer& cb) {
            using namespace threadmaxx;
            for (auto h : children) {
                cb.spawn(h, Transform{}, Velocity{}, RenderTag{}, UserData{},
                         Acceleration{}, Parent{p, Transform{}});
            }
        });
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;

    Engine engine(cfg);
    BatchGame game;
    CHECK(engine.initialize(game));

    const auto& world = engine.world();
    CHECK_EQ(world.size(), std::size_t{8});

    // Every seeded handle is alive and distinct.
    for (std::size_t i = 0; i < game.seeded.size(); ++i) {
        CHECK(world.alive(game.seeded[i]));
        for (std::size_t j = i + 1; j < game.seeded.size(); ++j) {
            CHECK(game.seeded[i].index != game.seeded[j].index);
        }
    }

    // Register a system that reserves 4 handles in update() and spawns
    // them as children of seeded[0].
    auto* direct = new BatchSpawnerDirect;
    direct->parent = game.seeded[0];
    engine.registerSystem(std::unique_ptr<BatchSpawnerDirect>(direct));

    engine.step();

    CHECK_EQ(direct->spawned.load(), std::uint32_t{4});
    CHECK_EQ(world.size(), std::size_t{12});

    // The 4 children should all carry the Parent bit pointing at seeded[0].
    std::size_t childCount = 0;
    for (auto h : world.entities()) {
        if (world.has<Parent>(h)) {
            const auto& p = world.get<Parent>(h);
            if (p.parent == game.seeded[0]) ++childCount;
        }
    }
    CHECK_EQ(childCount, std::size_t{4});

    // Zero-count reservation is a no-op.
    std::array<EntityHandle, 4> empty{};
    const auto n0 = engine.reserveEntityHandles(0, empty);
    CHECK_EQ(n0, std::uint32_t{0});
    for (auto h : empty) CHECK(!h.valid());

    // Span smaller than count: clamps to span size.
    std::array<EntityHandle, 2> small{};
    const auto nSmall = engine.reserveEntityHandles(10, small);
    CHECK_EQ(nSmall, std::uint32_t{2});
    CHECK(small[0].valid());
    CHECK(small[1].valid());

    engine.shutdown();
    EXIT_WITH_RESULT();
}
