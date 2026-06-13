/// @file test_editor_hierarchy_chain.cpp
/// @brief E12 — a 3-entity parent-child chain produces DFS rows at
/// depths 0, 1, 2 with the parent flag flipped on the intermediates.

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
    const auto rows = view.tree();
    CHECK_EQ(rows.size(), 3u);

    CHECK(rows[0].handle == game.root);
    CHECK_EQ(rows[0].depth, 0u);
    CHECK(rows[0].hasChildren);

    CHECK(rows[1].handle == game.mid);
    CHECK_EQ(rows[1].depth, 1u);
    CHECK(rows[1].hasChildren);

    CHECK(rows[2].handle == game.leaf);
    CHECK_EQ(rows[2].depth, 2u);
    CHECK(!rows[2].hasChildren);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
