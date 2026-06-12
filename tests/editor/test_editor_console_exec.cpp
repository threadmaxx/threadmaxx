/// @file test_editor_console_exec.cpp
/// @brief E10 — type a console command; the resulting IEditCommand
/// applies through the engine.

#include "Check.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/console.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/World.hpp>

#include <cstdlib>

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
    void undo (threadmaxx::CommandBuffer& cb) override {
        cb.setTransform(target, from);
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

    threadmaxx::editor::Console console;
    const auto handle = game.entity;
    console.registerCommand({
        "setTransform",
        [handle](std::span<const std::string> args)
            -> std::unique_ptr<threadmaxx::editor::IEditCommand> {
            if (args.size() != 3) return nullptr;
            threadmaxx::Transform t{};
            t.position = {std::strtof(args[0].c_str(), nullptr),
                          std::strtof(args[1].c_str(), nullptr),
                          std::strtof(args[2].c_str(), nullptr)};
            return std::make_unique<SetTransformCmd>(
                handle, threadmaxx::Transform{}, t);
        },
    });

    const auto r = console.exec(stack, "setTransform 5 0 0");
    CHECK(r == threadmaxx::editor::EditResult::Deferred);

    engine.step();
    CHECK_EQ(xOf(engine.world(), handle), 5.0f);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
