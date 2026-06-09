#pragma once

#include "threadmaxx_navmesh/types.hpp"

#include "threadmaxx/Components.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

/// Synchronous polygon-graph A* path query (N3). The service stores
/// results under an opaque `PathId` so the same shape works when N5
/// converts it into an asynchronous worker-fed solver.
namespace threadmaxx::navmesh {

using ::threadmaxx::Vec3;

class NavMeshRegistry;

/// Why the most recent `PathQueryService::request` failed before the
/// solver ran. `Accepted` means the solve ran; per-result `success`
/// reports whether the goal was reached.
enum class PathRequestStatus : std::uint8_t {
    Accepted,         ///< Request was solved (success/partial reported per-result).
    InvalidMesh,      ///< `mesh` ref doesn't resolve in the registry.
    StartNotOnMesh,   ///< `start` doesn't fall inside any polygon.
    GoalNotOnMesh,    ///< `goal` doesn't fall inside any polygon.
};

/// Inputs to a path query. `agentRadius` / `agentHeight` are reserved
/// for N7 (steering / corridor-shrink); the N3 solver ignores them.
struct PathRequest {
    NavMeshRef mesh{};
    Vec3 start{};
    Vec3 goal{};
    float agentRadius{};
    float agentHeight{};
    /// Bit `k` enables polygons tagged `areaTag == k`. Default = walk
    /// every area. The start polygon is always considered walkable.
    std::uint32_t areaMask{0xFFFFFFFFu};
    /// When true, an unreachable goal yields a best-effort path that
    /// ends at the polygon with the lowest heuristic-to-goal seen.
    /// When false, an unreachable goal sets `success = false`.
    bool allowPartial{true};
};

/// Output of a path query. `corridor` is the polygon sequence the path
/// walks; `waypoints` is `[start, edge-midpoints..., end]`. The end is
/// `goal` when the query reached it, or the centroid of the closest
/// reachable polygon for a partial result.
struct PathResult {
    /// Polygon along the corridor — `(tileId, polyId)` pair.
    struct CorridorEntry {
        NavTileId tileId;
        NavPolyId polyId;
    };

    PathId id{};                          ///< Matches the id returned by `request`.
    bool ready{};                         ///< Always true in v1.0 (sync); N5 makes this load-bearing.
    bool success{};                       ///< true if a usable path was produced.
    bool partial{};                       ///< true when `allowPartial` rescued an unreachable goal.
    std::vector<Vec3> waypoints;          ///< `[start, edge-midpoints..., end]`.
    std::vector<CorridorEntry> corridor;  ///< Polygon walk start→end (empty on failure).
    float cost{};                         ///< Sum of A* edge costs along the corridor.
};

/// Owns synchronous-mode path solves. The instance keeps a per-mesh
/// A* scratch — `request` is single-thread by API contract in v1.0;
/// `tryGet` / `cancel` / `clear` are safe from any thread.
class PathQueryService {
public:
    /// `registry` must outlive the service — only borrowed.
    explicit PathQueryService(const NavMeshRegistry& registry);
    ~PathQueryService();
    PathQueryService(const PathQueryService&) = delete;
    PathQueryService& operator=(const PathQueryService&) = delete;
    PathQueryService(PathQueryService&&) noexcept;
    PathQueryService& operator=(PathQueryService&&) noexcept;

    /// Solve immediately and stash the result. Returns the id, or 0
    /// when the request failed pre-solve (`lastRequestStatus` reports
    /// the reason).
    PathId request(const PathRequest& req);

    /// Look up a previously-solved result. `std::nullopt` if `id` is
    /// unknown (e.g. cancelled or never issued).
    std::optional<PathResult> tryGet(PathId id) const;

    /// Drop the stored result for `id`. No-op if the id is unknown.
    void cancel(PathId id);

    /// Drop every stored result.
    void clear();

    /// Number of stored results currently retrievable.
    std::size_t storedCount() const noexcept;

    /// Diagnostic — only meaningful from the thread that called
    /// `request` last (single-thread contract).
    PathRequestStatus lastRequestStatus() const noexcept { return lastStatus_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    PathRequestStatus lastStatus_{PathRequestStatus::Accepted};
};

} // namespace threadmaxx::navmesh
