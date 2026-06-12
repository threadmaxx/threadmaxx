/// @file test_editor_property_edit.cpp
/// @brief E7 — set Health.current = 7 via the property editor; the
/// resulting command applies on the next step; undo reverts.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/properties.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>

#include <threadmaxx_reflect/registry.hpp>
#include <threadmaxx_reflect/value.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        entity = engine.reserveEntityHandle();
        seed.spawn(entity, threadmaxx::Transform{});
        threadmaxx::Health h{};
        h.current = 50.0f; h.max = 100.0f;
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
    threadmaxx::editor::PropertyEditor ed{engine, reg};
    ed.addBuiltinBindings();
    CHECK(engine.initialize(game));
    engine.step();

    CHECK_EQ(currentOf(engine.world(), game.entity), 50.0f);

    const auto r = ed.setField(
        stack, game.entity, "Health", "current",
        threadmaxx::reflect::Value::make<float>(7.0f));
    CHECK(r == threadmaxx::editor::EditResult::Deferred);

    engine.step();
    CHECK_EQ(currentOf(engine.world(), game.entity), 7.0f);

    CHECK(stack.canUndo());
    stack.undo();
    engine.step();
    CHECK_EQ(currentOf(engine.world(), game.entity), 50.0f);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
