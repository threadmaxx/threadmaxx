/// @file panels/NavmeshPanel.cpp
/// @brief ST20 — `NavmeshPanel` implementation.

#include <threadmaxx_studio/panels/navmesh.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_navmesh/diagnostics.hpp>
#include <threadmaxx_navmesh/mesh.hpp>
#include <threadmaxx_navmesh/obstacle.hpp>
#include <threadmaxx_navmesh/query.hpp>

#include <cstdio>

namespace threadmaxx::studio {

NavmeshPanel::NavmeshPanel(const navmesh::PathQueryService& queries,
                           const navmesh::ObstacleOverlay&  obstacles,
                           const navmesh::NavMeshRegistry&  meshes) noexcept
    : queries_(&queries), obstacles_(&obstacles), meshes_(&meshes) {}

void NavmeshPanel::setSources(const navmesh::PathQueryService* queries,
                              const navmesh::ObstacleOverlay*  obstacles,
                              const navmesh::NavMeshRegistry*  meshes) noexcept {
    queries_   = queries;
    obstacles_ = obstacles;
    meshes_    = meshes;
}

bool NavmeshPanel::isBound() const noexcept {
    return queries_ != nullptr && obstacles_ != nullptr && meshes_ != nullptr;
}

void NavmeshPanel::render(editor::IEditorBackend& backend,
                          IStudioDataSource&) {
    if (!isBound()) {
        backend.drawText("Navmesh: <detached>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }
    const auto s = navmesh::sampleStats(*queries_, *obstacles_, *meshes_);

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Navmesh  workers=%u  meshes=%zu",
                  s.pathWorkerCount, s.navMeshCount);
    backend.drawText(buf, 0.0f, 0.0f);

    std::snprintf(buf, sizeof(buf),
                  "queue=%zu  stored=%zu  obstacles=%zu",
                  s.pendingPathQueries, s.storedPathResults,
                  s.obstacleCount);
    backend.drawText(buf, 0.0f, 16.0f);

    lastRows_ = 2;
}

} // namespace threadmaxx::studio
