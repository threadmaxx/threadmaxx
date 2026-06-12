/// @file test_navmesh_diagnostics.cpp
/// @brief N10 — sampleStats over PathQueryService + ObstacleOverlay +
/// NavMeshRegistry returns the per-subsystem counters in one POD.

#include "Check.hpp"

#include <threadmaxx_navmesh/diagnostics.hpp>
#include <threadmaxx_navmesh/mesh.hpp>
#include <threadmaxx_navmesh/obstacle.hpp>
#include <threadmaxx_navmesh/query.hpp>

int main() {
    using namespace threadmaxx::navmesh;

    NavMeshRegistry meshes;
    ObstacleOverlay obstacles;
    PathQueryService::Config cfg{};
    cfg.workerThreads = 0;  // sync mode keeps the test deterministic
    PathQueryService queries{meshes, cfg};

    // Empty baseline.
    auto s = sampleStats(queries, obstacles, meshes);
    CHECK_EQ(s.navMeshCount, 0u);
    CHECK_EQ(s.obstacleCount, 0u);
    CHECK_EQ(s.pendingPathQueries, 0u);
    CHECK_EQ(s.storedPathResults, 0u);
    CHECK_EQ(s.pathWorkerCount, 0u);

    // Add a couple of obstacles.
    DynamicObstacle o{};
    o.center = {0.0f, 0.0f, 0.0f};
    o.halfExtents = {1.0f, 1.0f, 1.0f};
    const auto h1 = obstacles.add(o);
    const auto h2 = obstacles.add(o);
    CHECK(h1 != 0u);
    CHECK(h2 != 0u);

    s = sampleStats(queries, obstacles, meshes);
    CHECK_EQ(s.obstacleCount, 2u);

    // Worker count snapshot reflects the config.
    PathQueryService::Config cfgT{};
    cfgT.workerThreads = 2;
    PathQueryService queriesT{meshes, cfgT};
    const auto sT = sampleStats(queriesT, obstacles, meshes);
    CHECK_EQ(sT.pathWorkerCount, 2u);

    EXIT_WITH_RESULT();
}
