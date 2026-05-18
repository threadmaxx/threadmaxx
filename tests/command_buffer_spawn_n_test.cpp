// §3.10.3 batch 23 — F12 regression test: `CommandBuffer::spawnBundleN`
// bulk-spawn helper. Verifies:
//   - N spawn commands fire when both spans are length N.
//   - The shorter span bounds the count (mismatched-length inputs).
//   - The reserved handles materialize into live entities post-commit.
//   - Per-entity Bundle masks survive the round-trip.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <vector>

namespace {
struct SeedGame : threadmaxx::IGame {
    std::vector<threadmaxx::EntityHandle> handles;
    void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        constexpr std::size_t N = 16;
        handles.resize(N);
        const auto got = e.reserveEntityHandles(
            static_cast<std::uint32_t>(N),
            std::span<threadmaxx::EntityHandle>(handles.data(), N));
        (void)got;

        std::vector<threadmaxx::Bundle> bundles;
        bundles.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            threadmaxx::Bundle b{};
            b.transform.position.x = static_cast<float>(i) * 1.5f;
            b.transform.position.y = 1.0f;
            // Mix two archetype shapes so the engine has to manage
            // multiple destination chunks during the bulk apply.
            if ((i % 2) == 0) {
                b.initialMask = threadmaxx::ComponentSet{
                    threadmaxx::Component::Transform,
                    threadmaxx::Component::Velocity};
            } else {
                b.initialMask = threadmaxx::ComponentSet{
                    threadmaxx::Component::Transform,
                    threadmaxx::Component::Health};
                b.health = threadmaxx::Health{42.0f, 42.0f};
            }
            bundles.push_back(b);
        }
        seed.spawnBundleN(
            std::span<const threadmaxx::EntityHandle>(handles),
            std::span<const threadmaxx::Bundle>(bundles));
    }
};
} // namespace

int main() {
    using namespace threadmaxx;
    Config cfg; cfg.sleepToPace = false;
    Engine engine(cfg);
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();   // commit the seed buffer

    // All 16 reserved handles are now live.
    CHECK_EQ(game.handles.size(), std::size_t{16});
    int evenCount = 0, oddCount = 0;
    for (std::size_t i = 0; i < game.handles.size(); ++i) {
        const auto h = game.handles[i];
        CHECK(engine.world().alive(h));
        // Even = Transform+Velocity, odd = Transform+Health.
        if (i % 2 == 0) {
            CHECK( engine.world().has<Velocity>(h));
            CHECK(!engine.world().has<Health>(h));
            ++evenCount;
        } else {
            CHECK( engine.world().has<Health>(h));
            CHECK(!engine.world().has<Velocity>(h));
            const auto& hp = engine.world().get<Health>(h);
            CHECK_EQ(hp.current, 42.0f);
            CHECK_EQ(hp.max,     42.0f);
            ++oddCount;
        }
    }
    CHECK_EQ(evenCount, 8);
    CHECK_EQ(oddCount,  8);
    std::printf("[command_buffer_spawn_n] 16 entities spawned (8 + 8 split)\n");

    // Mismatched-span behavior: shorter span bounds. Run as a second
    // pass via a single() callback to keep the test self-contained.
    {
        std::vector<EntityHandle> reserved(4);
        const auto got = engine.reserveEntityHandles(
            4u, std::span<EntityHandle>(reserved.data(), 4u));
        CHECK_EQ(got, 4u);
        // Only supply 2 bundles → only 2 spawns fire; the last 2
        // reservations get reaped at end-of-step.
        std::vector<Bundle> twoBundles(2);
        twoBundles[0].transform.position.x = 100.0f;
        twoBundles[0].initialMask = ComponentSet{Component::Transform};
        twoBundles[1].transform.position.x = 200.0f;
        twoBundles[1].initialMask = ComponentSet{Component::Transform};

        struct PartialSpawn : ISystem {
            std::span<const EntityHandle> reserved;
            std::span<const Bundle>       bundles;
            int                           fired = 0;
            const char* name() const noexcept override { return "partial"; }
            ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
            ComponentSet writes() const noexcept override { return ComponentSet::all(); }
            void update(SystemContext& ctx) override {
                if (fired++ > 0) return;
                auto r = reserved;
                auto b = bundles;
                ctx.single([r, b](Range, CommandBuffer& cb) {
                    cb.spawnBundleN(r, b);
                });
            }
        };
        auto ps = std::make_unique<PartialSpawn>();
        ps->reserved = std::span<const EntityHandle>(reserved);
        ps->bundles  = std::span<const Bundle>(twoBundles);
        engine.registerSystem(std::move(ps));
        const auto sizeBefore = engine.world().size();
        engine.step();
        engine.step();   // reservation reap + spawn commit
        const auto sizeAfter = engine.world().size();
        std::printf("[command_buffer_spawn_n] partial: before=%zu after=%zu "
                    "(expected +2)\n",
                    sizeBefore, sizeAfter);
        CHECK_EQ(sizeAfter, sizeBefore + 2u);
    }

    EXIT_WITH_RESULT();
}
