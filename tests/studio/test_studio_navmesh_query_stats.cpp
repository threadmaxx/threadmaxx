/// @file test_studio_navmesh_query_stats.cpp
/// @brief ST20 — NavmeshPanel renders header + counters when bound;
/// placeholder otherwise.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/navmesh.hpp>

#include <threadmaxx_navmesh/mesh.hpp>
#include <threadmaxx_navmesh/obstacle.hpp>
#include <threadmaxx_navmesh/query.hpp>

namespace {

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::NavmeshPanel panel;
    CHECK(!panel.isBound());

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    navmesh::NavMeshRegistry  registry;
    navmesh::ObstacleOverlay  obstacles;
    navmesh::PathQueryService queries{registry};
    panel.setSources(&queries, &obstacles, &registry);
    CHECK(panel.isBound());

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 2u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 2u);

    // Detach one source → back to the placeholder branch.
    panel.setSources(&queries, nullptr, &registry);
    CHECK(!panel.isBound());
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
