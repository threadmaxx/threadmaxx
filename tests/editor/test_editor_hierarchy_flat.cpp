/// @file test_editor_hierarchy_flat.cpp
/// @brief E12 — every entity without `Parent` lands at depth 0; the
/// returned ordering matches `world.entities()`.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/hierarchy.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>

namespace {

struct FlatGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 5; ++i) {
            threadmaxx::Transform t{};
            t.position = {static_cast<float>(i), 0, 0};
            seed.spawn(t);
        }
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    FlatGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::HierarchyView view{engine};
    const auto rows = view.tree();
    CHECK_EQ(rows.size(), 5u);
    for (const auto& r : rows) {
        CHECK_EQ(r.depth, 0u);
        CHECK(!r.hasChildren);
        CHECK(r.expanded);
        CHECK(!r.label.empty());
    }
    CHECK_EQ(view.collapsedCount(), 0u);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
