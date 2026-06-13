#pragma once

/// @file panels/navmesh.hpp
/// @brief ST20 — `NavmeshPanel` reads `navmesh::sampleStats` (N10)
/// and renders one row per counter.
///
/// Holds non-owning pointers to the three load-bearing navmesh
/// surfaces (PathQueryService / ObstacleOverlay / NavMeshRegistry).
/// All three must be bound for the panel to sample; otherwise the
/// panel renders a "detached" placeholder.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::navmesh {
class PathQueryService;
class ObstacleOverlay;
class NavMeshRegistry;
} // namespace threadmaxx::navmesh

namespace threadmaxx::studio {

class NavmeshPanel : public IStudioPanel {
public:
    NavmeshPanel() noexcept = default;
    NavmeshPanel(const navmesh::PathQueryService& queries,
                 const navmesh::ObstacleOverlay&  obstacles,
                 const navmesh::NavMeshRegistry&  meshes) noexcept;

    /// @brief Bind / unbind in one call. Pass nullptr to detach.
    void setSources(const navmesh::PathQueryService* queries,
                    const navmesh::ObstacleOverlay*  obstacles,
                    const navmesh::NavMeshRegistry*  meshes) noexcept;

    std::string_view id() const noexcept override {
        return "sibling.navmesh";
    }
    std::string_view title() const noexcept override { return "Navmesh"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    [[nodiscard]] bool        isBound()  const noexcept;

private:
    const navmesh::PathQueryService* queries_{nullptr};
    const navmesh::ObstacleOverlay*  obstacles_{nullptr};
    const navmesh::NavMeshRegistry*  meshes_{nullptr};
    std::size_t                      lastRows_{0};
};

} // namespace threadmaxx::studio
