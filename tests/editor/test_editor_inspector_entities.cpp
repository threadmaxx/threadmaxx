/// @file test_editor_inspector_entities.cpp
/// @brief E2 — engine with N spawned entities → listEntities() returns
/// N summaries with correct component lists.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/inspect.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>

namespace {

struct SpawnGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 10; ++i) {
            threadmaxx::Transform t{};
            t.position = {static_cast<float>(i), 0, 0};
            seed.spawn(t);
        }
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SpawnGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::Inspector ins{engine};
    const auto rows = ins.listEntities();
    CHECK_EQ(rows.size(), 10u);

    for (const auto& row : rows) {
        CHECK(row.handle.valid());
        CHECK(!row.label.empty());
        // Every spawn went through the Transform-only overload.
        bool hasTransform = false;
        for (const auto& c : row.components) {
            if (c == "Transform") { hasTransform = true; break; }
        }
        CHECK(hasTransform);
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
