/// @file test_editor_gizmo_drag_translate.cpp
/// @brief E8 — drag the X handle by 5 world units; resulting command
/// applies +5 along X via the engine's CommandBuffer path.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/gizmo.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

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

float xOf(const threadmaxx::World& w, threadmaxx::EntityHandle e) {
    const auto* t = w.tryGetTransform(e);
    return t ? t->position.x : -999.0f;
}

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    threadmaxx::editor::CommandStack stack{engine};
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::TranslateGizmo giz;
    CHECK(giz.beginDrag(threadmaxx::editor::GizmoAxis::X));
    auto r = giz.updateDrag(5.0f);
    CHECK(r.has_value());
    CHECK_EQ(r->delta.x, 5.0f);

    threadmaxx::Transform oldT = *engine.world().tryGetTransform(game.entity);
    threadmaxx::Transform newT = oldT;
    newT.position = newT.position + r->delta;

    stack.execute(threadmaxx::editor::TranslateGizmo::makeTranslateCommand(
        game.entity, oldT, newT));
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 5.0f);

    // Undo round-trips.
    stack.undo();
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 0.0f);

    giz.endDrag();
    CHECK(giz.activeAxis() == threadmaxx::editor::GizmoAxis::None);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
