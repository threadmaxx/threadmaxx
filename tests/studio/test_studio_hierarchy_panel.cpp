/// @file test_studio_hierarchy_panel.cpp
/// @brief ST7 — `HierarchyPanel` renders one row per visible node,
/// indents by depth, flips chevron on `toggleRow`, and forwards
/// `selectRow` to the borrowed `SelectionState`.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/hierarchy.hpp>
#include <threadmaxx_editor/selection.hpp>
#include <threadmaxx_studio/panels/hierarchy.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/World.hpp>

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

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    ChainGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::HierarchyView view{engine};
    threadmaxx::editor::SelectionState selection{engine.world()};
    threadmaxx::studio::HierarchyPanel panel{view, selection};

    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource source;

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 3u);

    const auto& frame = backend.capturedFrame();
    CHECK_EQ(countTextOps(frame), 3u);

    // depth 0/1/2 produce indents at 0, 12, 24 px.
    bool sawRoot = false, sawMid = false, sawLeaf = false;
    for (const auto& op : frame.ops) {
        if (op.op != threadmaxx::editor::CapturedOp::Op::DrawText) continue;
        if (op.x == 0.0f)   sawRoot = true;
        if (op.x == 12.0f)  sawMid  = true;
        if (op.x == 24.0f)  sawLeaf = true;
    }
    CHECK(sawRoot);
    CHECK(sawMid);
    CHECK(sawLeaf);

    // Out-of-range select rejected.
    CHECK(!panel.selectRow(99));
    CHECK(selection.currentSelection().kind ==
          threadmaxx::editor::SelectionKind::None);

    // Click the leaf row → SelectionState updates.
    CHECK(panel.selectRow(2));
    CHECK(selection.currentSelection().kind ==
          threadmaxx::editor::SelectionKind::Entity);

    // Toggle the mid row collapsed → next render drops the leaf row.
    CHECK(panel.toggleRow(1));
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 2u);
    CHECK(!view.isExpanded(game.mid));

    // Out-of-range toggle rejected after collapse (only 2 rows now).
    CHECK(!panel.toggleRow(5));

    backend.shutdown();
    engine.shutdown();
    EXIT_WITH_RESULT();
}
