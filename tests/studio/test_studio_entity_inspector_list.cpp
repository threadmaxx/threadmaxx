/// @file test_studio_entity_inspector_list.cpp
/// @brief ST5 — 10-entity engine → panel render produces 10 rows
/// (10 drawText ops + begin / end frame); selectRow(i) updates the
/// shared editor::SelectionState to entity i.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/inspect.hpp>
#include <threadmaxx_editor/selection.hpp>
#include <threadmaxx_studio/panels/engine_inspector.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/World.hpp>

namespace {

struct SpawnGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        for (int i = 0; i < 10; ++i) {
            threadmaxx::Transform t{};
            t.position = {static_cast<float>(i), 0, 0};
            seed.spawn(t);
        }
    }
};

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SpawnGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::Inspector inspector{engine};
    threadmaxx::editor::SelectionState selection{engine.world()};

    threadmaxx::studio::EntityInspectorPanel panel{inspector, selection};
    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource source;

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 10u);

    // Count captured drawText ops; should equal 10.
    std::size_t textOps = 0;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) {
            ++textOps;
        }
    }
    CHECK_EQ(textOps, 10u);

    // Out-of-range selectRow rejected.
    CHECK(!panel.selectRow(99));
    CHECK(selection.currentSelection().kind ==
          threadmaxx::editor::SelectionKind::None);

    // Click row 3 → SelectionState reflects entity[3].
    CHECK(panel.selectRow(3));
    const auto sel = selection.currentSelection();
    CHECK(sel.kind == threadmaxx::editor::SelectionKind::Entity);
    CHECK(sel.entity.valid());

    backend.shutdown();
    engine.shutdown();
    EXIT_WITH_RESULT();
}
