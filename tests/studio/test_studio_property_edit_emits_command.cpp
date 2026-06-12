/// @file test_studio_property_edit_emits_command.cpp
/// @brief ST6 — select entity → edit Health.current via panel →
/// command lands in CommandStack → engine sees the new value after
/// step → undo reverts. Mirrors the editor's E7 PropertyEditor test
/// shape (Health.current is the primitive-field-on-component pattern
/// PropertyEditor's writePrimitive supports).

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/properties.hpp>
#include <threadmaxx_editor/selection.hpp>
#include <threadmaxx_studio/panels/property_editor.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/World.hpp>

#include <threadmaxx_reflect/registry.hpp>
#include <threadmaxx_reflect/value.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        entity = engine.reserveEntityHandle();
        seed.spawn(entity, threadmaxx::Transform{});
        threadmaxx::Health h{};
        h.current = 50.0f;
        h.max = 100.0f;
        seed.setHealth(entity, h);
    }
};

float currentOf(const threadmaxx::World& w,
                threadmaxx::EntityHandle e) {
    const auto* h = w.tryGetHealth(e);
    return h ? h->current : -999.0f;
}

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    threadmaxx::editor::CommandStack stack{engine};
    threadmaxx::reflect::TypeRegistry reg;
    threadmaxx::editor::PropertyEditor editor{engine, reg};
    editor.addBuiltinBindings();
    threadmaxx::editor::SelectionState selection{engine.world()};
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::studio::PropertyEditorPanel panel{editor, selection, stack};

    // Rejected when no selection.
    auto r = panel.editField("Health", "current",
        threadmaxx::reflect::Value::make<float>(7.0f));
    CHECK(r == threadmaxx::editor::EditResult::Rejected);

    // Select the spawn target then edit.
    selection.select(game.entity);
    CHECK_EQ(currentOf(engine.world(), game.entity), 50.0f);

    r = panel.editField("Health", "current",
        threadmaxx::reflect::Value::make<float>(7.0f));
    CHECK(r == threadmaxx::editor::EditResult::Deferred);

    engine.step();
    CHECK_EQ(currentOf(engine.world(), game.entity), 7.0f);

    // Undo reverts.
    CHECK(stack.canUndo());
    stack.undo();
    engine.step();
    CHECK_EQ(currentOf(engine.world(), game.entity), 50.0f);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
