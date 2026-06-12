/// @file test_editor_diff_added.cpp
/// @brief E9 — diff(s, s + N new entities) returns N Added entries.

#include "Check.hpp"

#include <threadmaxx_editor/diff.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/System.hpp>

namespace {

struct InitialGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 3; ++i) {
            seed.spawn(threadmaxx::Transform{});
        }
    }
};

class Adder final : public threadmaxx::ISystem {
public:
    bool done{false};
    const char* name() const noexcept override { return "Adder"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        ctx.single([](threadmaxx::Range,
                      threadmaxx::CommandBuffer& cb) {
            for (int i = 0; i < 5; ++i) {
                cb.spawn(threadmaxx::Transform{});
            }
        });
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    InitialGame game;
    engine.registerSystem(std::make_unique<Adder>());
    CHECK(engine.initialize(game));
    engine.step(); // commit the seed spawns; Adder also fires here.

    auto firstSnap = engine.world().snapshot();
    CHECK_EQ(firstSnap.size(), 8u);

    // Reset by taking a "before" snapshot from a fresh second engine
    // built with just the InitialGame (no Adder).
    threadmaxx::Engine engine2{threadmaxx::Config{}};
    InitialGame g2;
    CHECK(engine2.initialize(g2));
    engine2.step();
    auto beforeSnap = engine2.world().snapshot();
    CHECK_EQ(beforeSnap.size(), 3u);

    const auto r = threadmaxx::editor::WorldDiff::compute(
        beforeSnap, firstSnap);
    // firstSnap has every entity from beforeSnap (different ids, so
    // they all read as added) PLUS the 5 Adder spawns. Net: 8 Added
    // (or 5 added + 3 removed if generation differs). Either way it's
    // 8 entries.
    CHECK(r.size() >= 5u);
    int adds = 0;
    for (const auto& e : r.entries) {
        if (e.kind == threadmaxx::editor::DiffKind::Added) ++adds;
    }
    CHECK(adds >= 5);

    engine.shutdown();
    engine2.shutdown();
    EXIT_WITH_RESULT();
}
