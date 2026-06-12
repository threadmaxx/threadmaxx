/// @file test_studio_gizmo_translate_emits_command.cpp
/// @brief ST8 — Gizmo drag (programmatic) emits a SetTransform-style
/// IEditCommand through the CommandStack; engine sees the new
/// position after step(); undo reverts.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/gizmo.hpp>
#include <threadmaxx_editor/selection.hpp>
#include <threadmaxx_studio/panels/gizmo.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/World.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        entity = engine.reserveEntityHandle();
        seed.spawn(entity, threadmaxx::Transform{});
    }
};

float xOf(const threadmaxx::World& w, threadmaxx::EntityHandle e) {
    const auto* t = w.tryGetTransform(e);
    return t ? t->position.x : -999.0f;
}

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    threadmaxx::editor::CommandStack stack{engine};
    threadmaxx::editor::SelectionState selection{engine.world()};
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::studio::GizmoPanel panel{engine, selection, stack};

    // No selection → commitTranslate is a no-op.
    CHECK(!panel.commitTranslate(threadmaxx::editor::GizmoAxis::X, 5.0f));

    selection.select(game.entity);
    CHECK_EQ(xOf(engine.world(), game.entity), 0.0f);

    CHECK(panel.commitTranslate(threadmaxx::editor::GizmoAxis::X, 5.0f));
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 5.0f);

    // Drag state cleared after commit.
    CHECK(panel.gizmo().activeAxis() == threadmaxx::editor::GizmoAxis::None);

    // Undo round-trip.
    CHECK(stack.canUndo());
    stack.undo();
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 0.0f);

    // None axis → rejected.
    CHECK(!panel.commitTranslate(threadmaxx::editor::GizmoAxis::None, 1.0f));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
