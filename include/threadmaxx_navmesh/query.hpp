#pragma once

#include "threadmaxx_navmesh/types.hpp"

#include "threadmaxx/Components.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

/// Asynchronous polygon-graph A* path query (N5). `request()` enqueues
/// onto an internal worker queue; `tryGet()` returns `std::nullopt`
/// until the worker has produced a result; `wait()` blocks. Built on
/// top of the N3/N4 solver (corridor + funnel smoothing).
namespace threadmaxx::navmesh {

using ::threadmaxx::Vec3;

class NavMeshRegistry;
class ObstacleOverlay;

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
    /// Optional dynamic-obstacle overlay (N8). When non-null, the
    /// solver skips edges that cross into polygons whose centroid sits
    /// inside any obstacle blocking the neighbor's area tag. The
    /// overlay must outlive the request — the solver only borrows it.
    const ObstacleOverlay* obstacles{nullptr};
};

/// Output of a path query. `corridor` is the polygon sequence the path
/// walks; `waypoints` is the funnel-smoothed walking path `[start, ...,
/// end]`. The end is `goal` when the query reached it, or the centroid
/// of the closest reachable polygon for a partial result.
struct PathResult {
    /// Polygon along the corridor — `(tileId, polyId)` pair.
    struct CorridorEntry {
        NavTileId tileId;
        NavPolyId polyId;
    };

    PathId id{};                          ///< Matches the id returned by `request`.
    bool ready{};                         ///< Always true once `tryGet` / `wait` returns a value.
    bool success{};                       ///< true if a usable path was produced.
    bool partial{};                       ///< true when `allowPartial` rescued an unreachable goal.
    std::vector<Vec3> waypoints;          ///< funnel-smoothed `[start, ..., end]`.
    std::vector<CorridorEntry> corridor;  ///< Polygon walk start→end (empty on failure).
    float cost{};                         ///< Sum of A* edge costs along the corridor.
};

/// Tunables for the worker queue. `workerThreads = 0` runs solves
/// in-line on the `request()` thread (synchronous compat mode).
struct PathQueryServiceConfig {
    std::uint32_t workerThreads{1};
};

/// Owns the async path query queue. `request()` validates the inputs
/// up front (so `lastRequestStatus()` is meaningful on return), then
/// enqueues for an internal worker thread to solve. `tryGet` /
/// `wait` / `cancel` / `clear` are safe from any thread.
class PathQueryService {
public:
    using Config = PathQueryServiceConfig;

    /// `registry` must outlive the service — only borrowed.
    explicit PathQueryService(const NavMeshRegistry& registry,
                              Config cfg = {});
    ~PathQueryService();
    PathQueryService(const PathQueryService&) = delete;
    PathQueryService& operator=(const PathQueryService&) = delete;
    // Non-movable: holding worker threads + condvars makes a sound
    // move() awkward and we never relocate live services in practice.
    PathQueryService(PathQueryService&&) = delete;
    PathQueryService& operator=(PathQueryService&&) = delete;

    /// Validate (locate start/goal) then enqueue. Returns the id, or 0
    /// when the request failed pre-solve (`lastRequestStatus` reports
    /// the reason). In `workerThreads == 0` mode the solve runs inline
    /// and the result is ready before this call returns.
    PathId request(const PathRequest& req);

    /// Look up a result. `std::nullopt` if `id` is unknown, cancelled,
    /// or still queued / mid-solve.
    std::optional<PathResult> tryGet(PathId id) const;

    /// Block until a result for `id` is ready, the id is cancelled, or
    /// `timeout` elapses. Returns the result on success; `nullopt`
    /// otherwise. Safe to call from any thread.
    std::optional<PathResult> wait(
        PathId id, std::chrono::milliseconds timeout) const;

    /// Cancel a pending or completed request. If the request is still
    /// queued, the worker skips it on pop. If it's mid-solve, the
    /// worker discards the result on store. If it's already stored,
    /// the result is dropped. No-op for unknown ids.
    void cancel(PathId id);

    /// Drop every stored result AND every queued / in-flight request.
    /// In-flight solves complete but their result is discarded.
    void clear();

    /// Number of stored (ready) results currently retrievable.
    std::size_t storedCount() const noexcept;

    /// Number of requests queued but not yet solved. In-flight (already
    /// popped, mid-solve) requests are NOT counted.
    std::size_t pendingCount() const noexcept;

    /// Configured worker thread count. `0` means synchronous mode.
    std::uint32_t workerCount() const noexcept;

    /// Diagnostic — only meaningful from the thread that called
    /// `request` last (single-thread contract).
    PathRequestStatus lastRequestStatus() const noexcept { return lastStatus_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    PathRequestStatus lastStatus_{PathRequestStatus::Accepted};
};

} // namespace threadmaxx::navmesh
