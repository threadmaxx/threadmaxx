/// @file test_editor_hierarchy_collapse.cpp
/// @brief E12 — collapsing an intermediate node hides its descendants
/// in `tree()` but keeps the collapsed node itself visible.

#include "Check.hpp"

#include <threadmaxx_editor/hierarchy.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>

namespace {

struct ChainGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle root;
    threadmaxx::EntityHandle mid;
    threadmaxx::EntityHandle leaf;

    void onSetup(threadmaxx::Engine& eng,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        root = eng.reserveEntityHandle();
        mid  = eng.reserveEntityHandle();
        leaf = eng.reserveEntityHandle();

        cb.spawn(root, threadmaxx::Transform{});

        threadmaxx::Parent pMid;
        pMid.parent = root;
        cb.spawn(mid, threadmaxx::Transform{}, {}, {}, {}, {}, pMid);

        threadmaxx::Parent pLeaf;
        pLeaf.parent = mid;
        cb.spawn(leaf, threadmaxx::Transform{}, {}, {}, {}, {}, pLeaf);
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    ChainGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::HierarchyView view{engine};

    // Baseline: every node visible, none marked collapsed.
    {
        const auto rows = view.tree();
        CHECK_EQ(rows.size(), 3u);
        CHECK_EQ(view.collapsedCount(), 0u);
        CHECK(view.isExpanded(game.mid));
    }

    // Collapse mid → leaf disappears, root + mid stay; mid's
    // `expanded` flag reflects the state for the panel chevron.
    view.setExpanded(game.mid, false);
    {
        const auto rows = view.tree();
        CHECK_EQ(rows.size(), 2u);
        CHECK(rows[0].handle == game.root);
        CHECK(rows[1].handle == game.mid);
        CHECK(!rows[1].expanded);
        CHECK_EQ(view.collapsedCount(), 1u);
        CHECK(!view.isExpanded(game.mid));
    }

    // Expand mid again → leaf reappears.
    view.setExpanded(game.mid, true);
    {
        const auto rows = view.tree();
        CHECK_EQ(rows.size(), 3u);
        CHECK_EQ(view.collapsedCount(), 0u);
    }

    // Collapsing root hides everything below it.
    view.setExpanded(game.root, false);
    {
        const auto rows = view.tree();
        CHECK_EQ(rows.size(), 1u);
        CHECK(rows[0].handle == game.root);
        CHECK(!rows[0].expanded);
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
