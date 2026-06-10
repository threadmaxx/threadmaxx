#include "threadmaxx_navmesh/query.hpp"

#include "threadmaxx_navmesh/detail/a_star.hpp"
#include "threadmaxx_navmesh/detail/funnel.hpp"
#include "threadmaxx_navmesh/mesh.hpp"

#include "threadmaxx/Components.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace threadmaxx::navmesh {

namespace {

float distance(const Vec3& a, const Vec3& b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Vec3 polygonCentroid(const NavTile& tile, NavPolyId polyId) {
    const NavPoly& p = tile.polygons[polyId];
    Vec3 sum{};
    for (std::uint16_t k = 0; k < p.indexCount; ++k) {
        const std::uint32_t vi =
            tile.vertexIndices[std::size_t{p.indexStart} + k];
        const Vec3& v = tile.vertices[vi];
        sum.x += v.x;
        sum.y += v.y;
        sum.z += v.z;
    }
    const float inv = 1.0f / static_cast<float>(p.indexCount);
    return Vec3{sum.x * inv, sum.y * inv, sum.z * inv};
}

/// 2D point-in-polygon (XZ plane) — even-odd ray test casting +X.
/// Polygon vertices are taken in the order `vertexIndices` lists them,
/// which is winding-independent for this test.
bool pointInPolygonXZ(const NavTile& tile, NavPolyId polyId, const Vec3& q) {
    const NavPoly& p = tile.polygons[polyId];
    const std::uint32_t n = p.indexCount;
    const std::size_t baseIdx = std::size_t{p.indexStart};
    bool inside = false;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t va = tile.vertexIndices[baseIdx + i];
        const std::uint32_t vb =
            tile.vertexIndices[baseIdx + ((i + 1u) % n)];
        const Vec3& a = tile.vertices[va];
        const Vec3& b = tile.vertices[vb];
        if ((a.z > q.z) != (b.z > q.z)) {
            const float t = (q.z - a.z) / (b.z - a.z);
            const float xCross = a.x + t * (b.x - a.x);
            if (q.x < xCross) inside = !inside;
        }
    }
    return inside;
}

struct PolyLocation {
    std::uint32_t tileIdx{};
    NavPolyId polyId{};
};

std::optional<PolyLocation> locate(const NavMesh& mesh, const Vec3& q) {
    for (std::size_t ti = 0; ti < mesh.tiles().size(); ++ti) {
        const NavTile& tile = mesh.tiles()[ti];
        // Cheap AABB cull in XZ (Y is unconstrained — the mesh may sit
        // at any height and the request may be a head-height ray hit).
        if (q.x < tile.aabbMin.x || q.x > tile.aabbMax.x) continue;
        if (q.z < tile.aabbMin.z || q.z > tile.aabbMax.z) continue;
        for (NavPolyId pi = 0;
             pi < static_cast<NavPolyId>(tile.polygons.size()); ++pi) {
            if (pointInPolygonXZ(tile, pi, q)) {
                return PolyLocation{static_cast<std::uint32_t>(ti), pi};
            }
        }
    }
    return std::nullopt;
}

bool areaAllowed(std::uint16_t areaTag, std::uint32_t areaMask) noexcept {
    return (areaMask & (std::uint32_t{1} << (areaTag & 31u))) != 0u;
}

/// Find the edge index on `(tile, polyId)` that connects to
/// `(neighborTileIdx, neighborPolyId)`. Returns the edge or
/// `kInvalidPolyIndex` if no such adjacency is registered (which would
/// indicate a corridor reconstruction bug, not a runtime error).
std::uint32_t findEdgeTo(const NavMesh& mesh,
                         std::uint32_t tileIdx,
                         NavPolyId polyId,
                         std::uint32_t neighborTileIdx,
                         NavPolyId neighborPolyId) {
    const NavTile& tile = mesh.tiles()[tileIdx];
    const NavPoly& p = tile.polygons[polyId];
    for (std::uint16_t e = 0; e < p.indexCount; ++e) {
        const std::uint32_t intra =
            tile.neighborPolys[std::size_t{p.indexStart} + e];
        if (tileIdx == neighborTileIdx && intra == neighborPolyId) {
            return e;
        }
        if (intra == kInvalidPolyIndex) {
            const auto cross = mesh.crossTileNeighbor(tile.id, polyId, e);
            if (!cross) continue;
            const auto idx = mesh.tileIndex(cross->tileId);
            if (!idx) continue;
            if (static_cast<std::uint32_t>(*idx) == neighborTileIdx &&
                cross->polyId == neighborPolyId) {
                return e;
            }
        }
    }
    return kInvalidPolyIndex;
}

/// A* + funnel scratch — one instance per worker thread (or one shared
/// instance in synchronous mode). All members are reused across solves
/// to preserve allocation capacity.
struct SolverScratch {
    detail::AStarState state;
    detail::NodeIndex nodeIndex;
    std::vector<Vec3> centroids;
    std::vector<detail::FunnelPortal> portalBuf;
};

/// Pre-validated request: start/goal already located on the mesh.
struct PreparedRequest {
    PathId id{};
    PathRequest req{};
    PolyLocation startLoc{};
    PolyLocation goalLoc{};
};

/// Run the A* + funnel pipeline on `req` (already validated against
/// `mesh`) and write the result into `out`. Scratch is consumed but
/// retains capacity for the next call.
void solvePrepared(const NavMesh& mesh,
                   const PreparedRequest& prep,
                   SolverScratch& scratch,
                   PathResult& out) {
    const PathRequest& req = prep.req;
    out.id = prep.id;
    out.ready = true;
    out.partial = false;
    out.success = false;
    out.cost = 0.0f;
    out.waypoints.clear();
    out.corridor.clear();

    scratch.nodeIndex.rebuild(mesh);
    const std::uint32_t totalNodes = scratch.nodeIndex.total();
    scratch.centroids.assign(totalNodes, Vec3{});
    for (std::size_t ti = 0; ti < mesh.tiles().size(); ++ti) {
        const NavTile& tile = mesh.tiles()[ti];
        for (NavPolyId pi = 0;
             pi < static_cast<NavPolyId>(tile.polygons.size()); ++pi) {
            const std::uint32_t node = scratch.nodeIndex.encode(
                static_cast<std::uint32_t>(ti), pi);
            scratch.centroids[node] = polygonCentroid(tile, pi);
        }
    }

    scratch.state.resize(totalNodes);
    scratch.state.reset();

    const std::uint32_t startNode =
        scratch.nodeIndex.encode(prep.startLoc.tileIdx, prep.startLoc.polyId);
    const std::uint32_t goalNode =
        scratch.nodeIndex.encode(prep.goalLoc.tileIdx, prep.goalLoc.polyId);

    std::uint32_t bestNode = startNode;
    float bestH = distance(scratch.centroids[startNode], req.goal);

    scratch.state.gCost[startNode] = 0.0f;
    scratch.state.open.push(detail::AStarHeapItem{startNode, 0.0f, bestH});

    bool reachedGoal = false;
    while (!scratch.state.open.empty()) {
        const detail::AStarHeapItem cur = scratch.state.open.pop();
        if (scratch.state.closed.testAndSet(cur.node)) continue;
        if (cur.node == goalNode) {
            reachedGoal = true;
            break;
        }
        const float h = distance(scratch.centroids[cur.node], req.goal);
        if (h < bestH) {
            bestH = h;
            bestNode = cur.node;
        }

        std::uint32_t tileIdx;
        NavPolyId polyId;
        scratch.nodeIndex.decode(cur.node, tileIdx, polyId);
        const NavTile& tile = mesh.tiles()[tileIdx];
        const NavPoly& p = tile.polygons[polyId];

        for (std::uint16_t e = 0; e < p.indexCount; ++e) {
            std::uint32_t nbrTileIdx = tileIdx;
            NavPolyId nbrPolyId{};
            const std::uint32_t intra =
                tile.neighborPolys[std::size_t{p.indexStart} + e];
            if (intra != kInvalidPolyIndex) {
                nbrPolyId = intra;
            } else {
                const auto cross =
                    mesh.crossTileNeighbor(tile.id, polyId, e);
                if (!cross) continue;
                const auto nbrIdx = mesh.tileIndex(cross->tileId);
                if (!nbrIdx) continue;
                nbrTileIdx = static_cast<std::uint32_t>(*nbrIdx);
                nbrPolyId = cross->polyId;
            }
            const NavPoly& nbrPoly =
                mesh.tiles()[nbrTileIdx].polygons[nbrPolyId];
            if (!areaAllowed(nbrPoly.areaTag, req.areaMask)) continue;

            const std::uint32_t nbrNode =
                scratch.nodeIndex.encode(nbrTileIdx, nbrPolyId);
            if (scratch.state.closed.test(nbrNode)) continue;
            const float stepCost = distance(
                scratch.centroids[cur.node], scratch.centroids[nbrNode]);
            const float newG = cur.gCost + stepCost;
            if (newG < scratch.state.gCost[nbrNode]) {
                scratch.state.gCost[nbrNode] = newG;
                scratch.state.cameFrom[nbrNode] = cur.node;
                const float newF =
                    newG + distance(scratch.centroids[nbrNode], req.goal);
                scratch.state.open.push(
                    detail::AStarHeapItem{nbrNode, newG, newF});
            }
        }
    }

    std::uint32_t endNode = detail::kInvalidNode;
    if (reachedGoal) {
        out.success = true;
        endNode = goalNode;
    } else if (req.allowPartial) {
        out.success = true;
        out.partial = true;
        endNode = bestNode;
    }

    if (!out.success) return;

    std::vector<std::uint32_t> corridorNodes;
    std::uint32_t n = endNode;
    std::uint32_t guard = totalNodes + 1u;
    while (n != detail::kInvalidNode && guard > 0u) {
        corridorNodes.push_back(n);
        if (n == startNode) break;
        n = scratch.state.cameFrom[n];
        --guard;
    }
    std::reverse(corridorNodes.begin(), corridorNodes.end());

    out.cost = scratch.state.gCost[endNode];
    if (!std::isfinite(out.cost)) out.cost = 0.0f;

    out.corridor.reserve(corridorNodes.size());
    for (std::size_t i = 0; i < corridorNodes.size(); ++i) {
        std::uint32_t curTileIdx;
        NavPolyId curPolyId;
        scratch.nodeIndex.decode(corridorNodes[i], curTileIdx, curPolyId);
        out.corridor.push_back(PathResult::CorridorEntry{
            mesh.tiles()[curTileIdx].id, curPolyId});
    }

    const Vec3 endPoint = reachedGoal
        ? req.goal
        : scratch.centroids[endNode];

    scratch.portalBuf.clear();
    scratch.portalBuf.reserve(corridorNodes.size() + 1u);
    scratch.portalBuf.push_back(
        detail::FunnelPortal{req.start, req.start});

    for (std::size_t i = 0; i + 1u < corridorNodes.size(); ++i) {
        std::uint32_t curTileIdx;
        NavPolyId curPolyId;
        scratch.nodeIndex.decode(corridorNodes[i], curTileIdx, curPolyId);
        std::uint32_t nextTileIdx;
        NavPolyId nextPolyId;
        scratch.nodeIndex.decode(
            corridorNodes[i + 1u], nextTileIdx, nextPolyId);
        const std::uint32_t edge = findEdgeTo(
            mesh, curTileIdx, curPolyId, nextTileIdx, nextPolyId);
        if (edge == kInvalidPolyIndex) continue;
        const NavTile& tile = mesh.tiles()[curTileIdx];
        const NavPoly& p = tile.polygons[curPolyId];
        const std::size_t baseIdx = std::size_t{p.indexStart};
        const std::uint32_t vaIdx =
            tile.vertexIndices[baseIdx + edge];
        const std::uint32_t vbIdx = tile.vertexIndices[
            baseIdx + ((edge + 1u) % p.indexCount)];
        scratch.portalBuf.push_back(detail::FunnelPortal{
            tile.vertices[vbIdx], tile.vertices[vaIdx]});
    }

    scratch.portalBuf.push_back(
        detail::FunnelPortal{endPoint, endPoint});

    detail::stringPullFunnel(scratch.portalBuf, out.waypoints);
}

} // namespace

struct PathQueryService::Impl {
    const NavMeshRegistry& reg;
    PathQueryServiceConfig cfg;

    mutable std::mutex mtx;
    std::condition_variable cv;

    std::deque<PreparedRequest> queue;
    std::unordered_map<PathId, PathResult> results;
    std::unordered_set<PathId> cancelled;
    // Ids that have been popped off the queue and are currently being
    // solved by a worker. `clear()` needs to tombstone these because
    // they're no longer in `queue` but their result hasn't landed.
    std::unordered_set<PathId> inFlight;
    PathId nextId{1};
    bool shutdown{false};

    // Synchronous-mode scratch (workerThreads == 0). Lives on the
    // calling thread so we don't pay condvar overhead in tests that
    // want the v0.x behavior.
    SolverScratch syncScratch;

    std::vector<std::thread> workers;

    explicit Impl(const NavMeshRegistry& r, PathQueryServiceConfig c)
        : reg(r), cfg(c) {}
};

PathQueryService::PathQueryService(const NavMeshRegistry& registry,
                                   Config cfg)
    : impl_(std::make_unique<Impl>(registry, cfg)) {
    impl_->workers.reserve(cfg.workerThreads);
    for (std::uint32_t i = 0; i < cfg.workerThreads; ++i) {
        impl_->workers.emplace_back([this]() {
            SolverScratch scratch;
            std::unique_lock<std::mutex> g(impl_->mtx);
            while (true) {
                impl_->cv.wait(g, [this]() {
                    return impl_->shutdown || !impl_->queue.empty();
                });
                if (impl_->shutdown && impl_->queue.empty()) return;

                PreparedRequest prep = std::move(impl_->queue.front());
                impl_->queue.pop_front();

                // Cancel-while-pending: skip the solve entirely. The
                // cancelled set acts as a tombstone — if cancel() was
                // called before we popped, we drop the id and move on.
                if (impl_->cancelled.erase(prep.id) > 0) {
                    impl_->cv.notify_all();
                    continue;
                }

                const NavMesh* mesh = impl_->reg.find(prep.req.mesh);
                if (!mesh) {
                    // Mesh was unloaded between request() and solve.
                    // Drop silently — waiters time out.
                    impl_->cv.notify_all();
                    continue;
                }

                impl_->inFlight.insert(prep.id);
                g.unlock();
                PathResult result;
                solvePrepared(*mesh, prep, scratch, result);
                g.lock();
                impl_->inFlight.erase(prep.id);

                // Cancel-during-solve OR clear(): discard the result.
                if (impl_->cancelled.erase(prep.id) > 0) {
                    impl_->cv.notify_all();
                    continue;
                }
                impl_->results.emplace(prep.id, std::move(result));
                impl_->cv.notify_all();
            }
        });
    }
}

PathQueryService::~PathQueryService() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        impl_->shutdown = true;
    }
    impl_->cv.notify_all();
    for (auto& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
}

PathId PathQueryService::request(const PathRequest& req) {
    lastStatus_ = PathRequestStatus::Accepted;

    const NavMesh* mesh = impl_->reg.find(req.mesh);
    if (!mesh) {
        lastStatus_ = PathRequestStatus::InvalidMesh;
        return 0;
    }
    const auto startLoc = locate(*mesh, req.start);
    if (!startLoc) {
        lastStatus_ = PathRequestStatus::StartNotOnMesh;
        return 0;
    }
    const auto goalLoc = locate(*mesh, req.goal);
    if (!goalLoc) {
        lastStatus_ = PathRequestStatus::GoalNotOnMesh;
        return 0;
    }

    PreparedRequest prep;
    prep.req = req;
    prep.startLoc = *startLoc;
    prep.goalLoc = *goalLoc;

    // Synchronous mode: solve on the caller thread and store the result
    // directly. Matches v0.x behavior exactly when workerThreads == 0.
    if (impl_->cfg.workerThreads == 0) {
        PathId id;
        {
            std::lock_guard<std::mutex> g(impl_->mtx);
            id = impl_->nextId++;
        }
        prep.id = id;
        PathResult result;
        solvePrepared(*mesh, prep, impl_->syncScratch, result);
        {
            std::lock_guard<std::mutex> g(impl_->mtx);
            impl_->results.emplace(id, std::move(result));
            impl_->cv.notify_all();
        }
        return id;
    }

    PathId id;
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        id = impl_->nextId++;
        prep.id = id;
        impl_->queue.push_back(std::move(prep));
    }
    impl_->cv.notify_all();
    return id;
}

std::optional<PathResult> PathQueryService::tryGet(PathId id) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    const auto it = impl_->results.find(id);
    if (it == impl_->results.end()) return std::nullopt;
    return it->second;
}

std::optional<PathResult> PathQueryService::wait(
    PathId id, std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> g(impl_->mtx);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto it = impl_->results.find(id);
        if (it != impl_->results.end()) return it->second;
        // Already cancelled — no result will ever arrive.
        if (impl_->cancelled.count(id) > 0) return std::nullopt;
        if (impl_->shutdown) return std::nullopt;
        if (impl_->cv.wait_until(g, deadline) == std::cv_status::timeout) {
            return std::nullopt;
        }
    }
}

void PathQueryService::cancel(PathId id) {
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        // If the result is already stored, drop it and stop — no
        // tombstone needed because there's nothing in flight.
        if (impl_->results.erase(id) > 0) {
            impl_->cv.notify_all();
            return;
        }
        // Otherwise mark the id as cancelled so the worker drops it on
        // pop OR on store. The worker erases the entry on consumption.
        impl_->cancelled.insert(id);
    }
    impl_->cv.notify_all();
}

void PathQueryService::clear() {
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        // Every queued id becomes a tombstone so workers that pop them
        // skip the solve.
        for (const auto& p : impl_->queue) {
            impl_->cancelled.insert(p.id);
        }
        impl_->queue.clear();
        // In-flight ids get tombstoned too — when the worker re-acquires
        // the lock after solve, it sees `cancelled.contains(id)` and
        // drops the result.
        for (PathId id : impl_->inFlight) {
            impl_->cancelled.insert(id);
        }
        // Every ready id is dropped outright.
        for (const auto& kv : impl_->results) {
            // Tombstone so `wait()` returns nullopt fast instead of
            // sitting until timeout.
            impl_->cancelled.insert(kv.first);
        }
        impl_->results.clear();
    }
    impl_->cv.notify_all();
}

std::size_t PathQueryService::storedCount() const noexcept {
    std::lock_guard<std::mutex> g(impl_->mtx);
    return impl_->results.size();
}

std::size_t PathQueryService::pendingCount() const noexcept {
    std::lock_guard<std::mutex> g(impl_->mtx);
    return impl_->queue.size();
}

std::uint32_t PathQueryService::workerCount() const noexcept {
    return impl_->cfg.workerThreads;
}

} // namespace threadmaxx::navmesh
