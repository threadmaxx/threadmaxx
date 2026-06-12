/// @file test_editor_diff_modified.cpp
/// @brief E9 — diff against a snapshot where an entity's Transform
/// changed returns one Modified entry with the per-field diff.

#include "Check.hpp"

#include <threadmaxx_editor/diff.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/System.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle target{};
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 3; ++i) {
            seed.spawn(threadmaxx::Transform{});
        }
        target = engine.reserveEntityHandle();
        threadmaxx::Transform t{};
        t.position = {1.0f, 0.0f, 0.0f};
        seed.spawn(target, t);
    }
};

struct OneShotMover final : threadmaxx::ISystem {
    threadmaxx::EntityHandle target;
    bool done{false};
    explicit OneShotMover(threadmaxx::EntityHandle t) : target(t) {}
    const char* name() const noexcept override { return "OneShotMover"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        const auto tgt = target;
        ctx.single([tgt](threadmaxx::Range,
                         threadmaxx::CommandBuffer& cb) {
            threadmaxx::Transform t{};
            t.position = {7.0f, 8.0f, 9.0f};
            cb.setTransform(tgt, t);
        });
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();

    auto before = engine.world().snapshot();

    engine.registerSystem(std::make_unique<OneShotMover>(game.target));
    engine.step();

    auto after = engine.world().snapshot();
    const auto r = threadmaxx::editor::WorldDiff::compute(before, after);
    CHECK_EQ(r.size(), 1u);
    CHECK(r.entries[0].kind == threadmaxx::editor::DiffKind::Modified);
    CHECK(r.entries[0].handle == game.target);
    bool sawTransform = false;
    for (const auto& c : r.entries[0].componentChanges) {
        if (c == "Transform") sawTransform = true;
    }
    CHECK(sawTransform);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
