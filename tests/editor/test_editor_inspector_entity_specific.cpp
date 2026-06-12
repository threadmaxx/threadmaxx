/// @file test_editor_inspector_entity_specific.cpp
/// @brief E2 — Inspector::entity(handle) returns Some for a live
/// handle, None for a stale one.

#include "Check.hpp"

#include <threadmaxx_editor/inspect.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Handles.hpp>

namespace {

struct SeedGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle reserved{};

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        reserved = engine.reserveEntityHandle();
        threadmaxx::Transform t{};
        seed.spawn(reserved, t);
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::Inspector ins{engine};

    auto found = ins.entity(game.reserved);
    CHECK(found.has_value());
    CHECK(found->handle == game.reserved);

    threadmaxx::EntityHandle bogus{42, 999};
    CHECK(!ins.entity(bogus).has_value());

    engine.shutdown();
    EXIT_WITH_RESULT();
}
