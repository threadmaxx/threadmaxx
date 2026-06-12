/// @file test_editor_command_clear.cpp
/// @brief E3 — clear() empties the undo history; canUndo / canRedo
/// both go false; historySize → 0.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

namespace {

struct NoOp final : threadmaxx::editor::IEditCommand {
    std::string_view name() const noexcept override { return "NoOp"; }
    void apply(threadmaxx::CommandBuffer&) override {}
    void undo (threadmaxx::CommandBuffer&) override {}
};

struct NoopGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    NoopGame game;
    threadmaxx::editor::CommandStack stack{engine};
    CHECK(engine.initialize(game));
    engine.step();

    stack.execute(std::make_unique<NoOp>());
    stack.execute(std::make_unique<NoOp>());
    engine.step();
    CHECK_EQ(stack.historySize(), 2u);
    CHECK(stack.canUndo());
    CHECK(!stack.canRedo());

    stack.clear();
    CHECK_EQ(stack.historySize(), 0u);
    CHECK(!stack.canUndo());
    CHECK(!stack.canRedo());

    engine.shutdown();
    EXIT_WITH_RESULT();
}
