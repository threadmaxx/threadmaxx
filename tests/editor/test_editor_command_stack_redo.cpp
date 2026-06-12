/// @file test_editor_command_stack_redo.cpp
/// @brief E3 — apply A, undo A, redo A → world matches post-A state.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/World.hpp>

namespace {

struct SetX final : threadmaxx::editor::IEditCommand {
    threadmaxx::EntityHandle target;
    float fromX, toX;
    SetX(threadmaxx::EntityHandle e, float a, float b)
        : target(e), fromX(a), toX(b) {}
    std::string_view name() const noexcept override { return "SetX"; }
    void apply(threadmaxx::CommandBuffer& cb) override {
        threadmaxx::Transform t{};
        t.position = {toX, 0, 0};
        cb.setTransform(target, t);
    }
    void undo(threadmaxx::CommandBuffer& cb) override {
        threadmaxx::Transform t{};
        t.position = {fromX, 0, 0};
        cb.setTransform(target, t);
    }
};

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
    const auto* tr = w.tryGetTransform(e);
    return tr ? tr->position.x : -999.0f;
}

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    threadmaxx::editor::CommandStack stack{engine};
    CHECK(engine.initialize(game));
    engine.step();

    stack.execute(std::make_unique<SetX>(game.entity, 0.0f, 7.0f));
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 7.0f);

    CHECK(stack.canUndo());
    stack.undo();
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 0.0f);

    CHECK(stack.canRedo());
    stack.redo();
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 7.0f);

    CHECK(!stack.canRedo());

    engine.shutdown();
    EXIT_WITH_RESULT();
}
