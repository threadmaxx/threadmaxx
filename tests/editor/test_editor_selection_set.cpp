/// @file test_editor_selection_set.cpp
/// @brief E6 — select(handle) → currentSelection() returns the same.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/selection.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        entity = engine.reserveEntityHandle();
        seed.spawn(entity, threadmaxx::Transform{});
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::SelectionState sel{engine.world()};
    CHECK(sel.currentSelection().kind == threadmaxx::editor::SelectionKind::None);

    sel.select(game.entity);
    const auto s = sel.currentSelection();
    CHECK(s.kind == threadmaxx::editor::SelectionKind::Entity);
    CHECK(s.entity == game.entity);

    sel.selectResource(0xDEADBEEFull);
    const auto s2 = sel.currentSelection();
    CHECK(s2.kind == threadmaxx::editor::SelectionKind::Resource);
    CHECK_EQ(s2.resourceId, 0xDEADBEEFull);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
