/// @file test_editor_command_basic.cpp
/// @brief E3 — execute a SetTransform command, world reflects the new
/// transform; undo reverts.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/World.hpp>

namespace {

struct SetTransform final : threadmaxx::editor::IEditCommand {
    threadmaxx::EntityHandle target;
    threadmaxx::Transform from;
    threadmaxx::Transform to;

    SetTransform(threadmaxx::EntityHandle e,
                 threadmaxx::Transform a,
                 threadmaxx::Transform b)
        : target(e), from(a), to(b) {}

    std::string_view name() const noexcept override { return "SetTransform"; }
    void apply(threadmaxx::CommandBuffer& cb) override {
        cb.setTransform(target, to);
    }
    void undo(threadmaxx::CommandBuffer& cb) override {
        cb.setTransform(target, from);
    }
};

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        entity = engine.reserveEntityHandle();
        threadmaxx::Transform t{};
        t.position = {0, 0, 0};
        seed.spawn(entity, t);
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

    CHECK_EQ(xOf(engine.world(), game.entity), 0.0f);

    threadmaxx::Transform after{};
    after.position = {5.0f, 0, 0};
    stack.execute(std::make_unique<SetTransform>(
        game.entity, threadmaxx::Transform{}, after));
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 5.0f);

    stack.undo();
    engine.step();
    CHECK_EQ(xOf(engine.world(), game.entity), 0.0f);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
