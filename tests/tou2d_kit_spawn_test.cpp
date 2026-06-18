// tou2d_kit_spawn_test — N2 (2026-06-18) HUD kit-glyph latch contract.
//
// Pins:
//   * HudSystem::update() walks `Pickup` chunks via the world view and
//     latches per-entity (transform.x, transform.y) into
//     `kitPositionsForTest()` for each active (state==0) kit.
//   * Respawning kits (state==0 but carrying `DisabledTag`) are skipped
//     entirely — the chunk-mask `DisabledTag` filter is the cheap
//     correctness gate.
//   * Latched count saturates at `kMaxKitGlyphs` so a level can't blow
//     the per-frame draw budget regardless of how many kits got seeded.
//
// Implementation notes:
//   * HudSystem's update() requires non-null localPlayer + ship user-
//     component ids. We register both even though we don't spawn any
//     ships; the early-out is "ids not yet registered", not "no ships
//     to walk."
//   * camera_ is nullptr — only update() is exercised, which doesn't
//     touch camera. buildRenderFrame() short-circuits on null camera.

#include "Check.hpp"

#include "../examples/tou2d/HudSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <cstdio>

namespace {

// A minimal IGame stand-in that wires up exactly the user components
// HudSystem inspects, spawns a controlled set of Pickup entities, and
// registers a borrowed HudSystem with the engine.
struct N2GameState {
    tou2d::UserComponentIds ids{};

    void registerComponents(threadmaxx::Engine& e) {
        ids.localPlayer = e.registerUserComponent<tou2d::LocalPlayer>();
        ids.ship        = e.registerUserComponent<tou2d::Ship>();
        ids.loadout     = e.registerUserComponent<tou2d::WeaponLoadout>();
        ids.pickup      = e.registerUserComponent<tou2d::Pickup>();
    }

    threadmaxx::EntityHandle spawnKit(threadmaxx::Engine& e,
                                      threadmaxx::CommandBuffer& cb,
                                      float x, float y,
                                      std::uint8_t state) const {
        const auto h = e.reserveEntityHandle();
        threadmaxx::Bundle b{};
        b.transform.position = {x, y, 0.0f};
        b.transform.scale    = {14.0f, 14.0f, 14.0f};
        b.initialMask = threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
        };
        cb.spawnBundle(h, b);
        tou2d::Pickup pk{};
        pk.kind  = static_cast<std::uint8_t>(tou2d::PickupKind::RepairKit);
        pk.state = state;
        threadmaxx::addUserComponent(cb, ids.pickup, h, pk);
        if (state == 1) {
            // Mirror RepairKitSystem's respawn shape: state-1 carries
            // DisabledTag so the entity hides from gameplay & is
            // skipped by the HUD glyph latch.
            cb.addTag(h, threadmaxx::Component::DisabledTag);
        }
        return h;
    }
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    threadmaxx::Engine engine(cfg);

    N2GameState state;

    // Override registerSystem to capture the HudSystem* before the
    // engine consumes the unique_ptr. We do this by extending the
    // IGame to publish the borrow.
    struct HostGame : threadmaxx::IGame {
        N2GameState& state;
        tou2d::HudSystem* hudBorrow = nullptr;
        explicit HostGame(N2GameState& s) noexcept : state(s) {}

        void onSetup(threadmaxx::Engine& engine,
                     threadmaxx::World&  /*world*/,
                     threadmaxx::CommandBuffer& seed) override {
            state.registerComponents(engine);
            state.spawnKit(engine, seed,  10.0f,  20.0f, /*state=*/0);
            state.spawnKit(engine, seed, -15.0f,  -5.0f, /*state=*/0);
            state.spawnKit(engine, seed,  30.0f,  30.0f, /*state=*/1);

            auto hud = std::make_unique<tou2d::HudSystem>(state.ids, nullptr);
            hudBorrow = hud.get();
            engine.registerSystem(std::move(hud));
        }
    } game(state);

    CHECK(engine.initialize(game));
    CHECK(game.hudBorrow != nullptr);

    // One step — onSetup commits ran during initialize; this step
    // drives HudSystem::update which walks the world and latches.
    engine.step();

    const auto positions = game.hudBorrow->kitPositionsForTest();
    CHECK_EQ(positions.size(), std::size_t{2});

    // Order is chunk-walk order — not a stable contract across
    // archetype evolution. Verify by an unordered comparison.
    bool sawA = false, sawB = false;
    for (const auto& [x, y] : positions) {
        if (std::abs(x -  10.0f) < 0.01f && std::abs(y -  20.0f) < 0.01f) sawA = true;
        if (std::abs(x +  15.0f) < 0.01f && std::abs(y -  -5.0f) < 0.01f) sawB = true;
        // Respawning kit's coordinates must NEVER appear.
        CHECK(!(std::abs(x - 30.0f) < 0.01f && std::abs(y - 30.0f) < 0.01f));
    }
    CHECK(sawA);
    CHECK(sawB);

    std::printf("[kit_spawn_test] latched %zu active kit(s)\n", positions.size());

    EXIT_WITH_RESULT();
}
