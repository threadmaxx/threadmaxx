/// @file test_editor_diff_identical.cpp
/// @brief E9 — diff(s, s) returns an empty result.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/diff.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 5; ++i) {
            seed.spawn(threadmaxx::Transform{});
        }
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();

    auto snap = engine.world().snapshot();
    const auto r = threadmaxx::editor::WorldDiff::compute(snap, snap);
    CHECK(r.empty());

    engine.shutdown();
    EXIT_WITH_RESULT();
}
