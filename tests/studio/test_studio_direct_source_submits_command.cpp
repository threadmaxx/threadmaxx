/// @file test_studio_direct_source_submits_command.cpp
/// @brief ST4 — submitEditCommand routes through editor::CommandStack;
/// the engine sees the mutation after engine.step(). Pins the
/// Shape A mutation funnel.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_studio/direct_data_source.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/World.hpp>

#include <memory>

namespace {

struct SetTransformCmd final : threadmaxx::editor::IEditCommand {
    threadmaxx::EntityHandle target;
    threadmaxx::Transform from, to;
    SetTransformCmd(threadmaxx::EntityHandle e,
                    threadmaxx::Transform f,
                    threadmaxx::Transform t)
        : target(e), from(f), to(t) {}
    std::string_view name() const noexcept override { return "setTransform"; }
    void apply(threadmaxx::CommandBuffer& cb) override {
        cb.setTransform(target, to);
    }
    void undo(threadmaxx::CommandBuffer& cb) override {
        cb.setTransform(target, from);
    }
};

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle entity{};
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        entity = engine.reserveEntityHandle();
        seed.spawn(entity, threadmaxx::Transform{});
    }
};

} // namespace

int main() {
    threadmaxx::Config cfg{};
    threadmaxx::Engine engine{cfg};
    SeedGame game;
    threadmaxx::editor::CommandStack stack{engine};
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::studio::DirectDataSource src{engine, stack};

    const auto pre = engine.world().tryGetTransform(game.entity);
    CHECK(pre != nullptr);
    CHECK(pre->position.x == 0.0f);

    threadmaxx::Transform target{};
    target.position = {5.0f, 0.0f, 0.0f};

    CHECK(src.submitEditCommand(
        std::make_unique<SetTransformCmd>(game.entity,
                                          threadmaxx::Transform{}, target)));

    // Null pointer is rejected.
    CHECK(!src.submitEditCommand(nullptr));

    // The opaque cross-attach-mode envelope is intentionally rejected
    // on the Direct path — panels go through submitEditCommand.
    CHECK(!src.submitCommand("noop"));

    engine.step();
    const auto post = engine.world().tryGetTransform(game.entity);
    CHECK(post != nullptr);
    CHECK_EQ(post->position.x, 5.0f);

    // Undo round-trip via the stack.
    CHECK(stack.canUndo());
    (void)stack.undo();
    engine.step();
    const auto undone = engine.world().tryGetTransform(game.entity);
    CHECK(undone != nullptr);
    CHECK_EQ(undone->position.x, 0.0f);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
