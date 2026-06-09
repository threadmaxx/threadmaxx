#include "threadmaxx_navmesh/query.hpp"

#include "threadmaxx_navmesh/detail/a_star.hpp"
#include "threadmaxx_navmesh/mesh.hpp"

#include "threadmaxx/Components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
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

Vec3 edgeMidpoint(const NavTile& tile, NavPolyId polyId,
                  std::uint32_t edgeIdx) {
    const NavPoly& p = tile.polygons[polyId];
    const std::size_t baseIdx = std::size_t{p.indexStart};
    const std::uint32_t va =
        tile.vertexIndices[baseIdx + edgeIdx];
    const std::uint32_t vb =
        tile.vertexIndices[baseIdx + ((edgeIdx + 1u) % p.indexCount)];
    const Vec3& a = tile.vertices[va];
    const Vec3& b = tile.vertices[vb];
    return Vec3{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
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

} // namespace

struct PathQueryService::Impl {
    const NavMeshRegistry& reg;
    mutable std::mutex mtx;
    std::unordered_map<PathId, PathResult> results;
    PathId nextId{1};

    // Reusable A* scratch — single-threaded by API contract in v1.0.
    detail::AStarState state;
    detail::NodeIndex nodeIndex;
    std::vector<Vec3> centroids;

    explicit Impl(const NavMeshRegistry& r) : reg(r) {}
};

PathQueryService::PathQueryService(const NavMeshRegistry& registry)
    : impl_(std::make_unique<Impl>(registry)) {}
PathQueryService::~PathQueryService() = default;
PathQueryService::PathQueryService(PathQueryService&&) noexcept = default;
PathQueryService& PathQueryService::operator=(PathQueryService&&) noexcept =
    default;

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

    // Rebuild the node index + centroid cache for this solve. Cheap
    // for the tile counts we currently target; N5 will hoist these out
    // of the hot path with a per-mesh cache invalidated on reload.
    impl_->nodeIndex.rebuild(*mesh);
    const std::uint32_t totalNodes = impl_->nodeIndex.total();
    impl_->centroids.assign(totalNodes, Vec3{});
    for (std::size_t ti = 0; ti < mesh->tiles().size(); ++ti) {
        const NavTile& tile = mesh->tiles()[ti];
        for (NavPolyId pi = 0;
             pi < static_cast<NavPolyId>(tile.polygons.size()); ++pi) {
            const std::uint32_t node = impl_->nodeIndex.encode(
                static_cast<std::uint32_t>(ti), pi);
            impl_->centroids[node] = polygonCentroid(tile, pi);
        }
    }

    impl_->state.resize(totalNodes);
    impl_->state.reset();

    const std::uint32_t startNode =
        impl_->nodeIndex.encode(startLoc->tileIdx, startLoc->polyId);
    const std::uint32_t goalNode =
        impl_->nodeIndex.encode(goalLoc->tileIdx, goalLoc->polyId);

    // Best-h node fallback for partial paths. The start dominates until
    // a popped node has a strictly lower heuristic. Initial best cost
    // is 0 so the partial path can degenerate to `[start, centroid(start)]`.
    std::uint32_t bestNode = startNode;
    float bestH = distance(impl_->centroids[startNode], req.goal);

    impl_->state.gCost[startNode] = 0.0f;
    impl_->state.open.push(detail::AStarHeapItem{startNode, 0.0f, bestH});

    bool reachedGoal = false;
    while (!impl_->state.open.empty()) {
        const detail::AStarHeapItem cur = impl_->state.open.pop();
        // Lazy decrement: a node may sit in the heap with a stale (higher)
        // gCost; the closed bitset short-circuits those.
        if (impl_->state.closed.testAndSet(cur.node)) continue;
        if (cur.node == goalNode) {
            reachedGoal = true;
            break;
        }
        const float h = distance(impl_->centroids[cur.node], req.goal);
        if (h < bestH) {
            bestH = h;
            bestNode = cur.node;
        }

        std::uint32_t tileIdx;
        NavPolyId polyId;
        impl_->nodeIndex.decode(cur.node, tileIdx, polyId);
        const NavTile& tile = mesh->tiles()[tileIdx];
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
                    mesh->crossTileNeighbor(tile.id, polyId, e);
                if (!cross) continue;
                const auto nbrIdx = mesh->tileIndex(cross->tileId);
                if (!nbrIdx) continue;
                nbrTileIdx = static_cast<std::uint32_t>(*nbrIdx);
                nbrPolyId = cross->polyId;
            }
            const NavPoly& nbrPoly =
                mesh->tiles()[nbrTileIdx].polygons[nbrPolyId];
            if (!areaAllowed(nbrPoly.areaTag, req.areaMask)) continue;

            const std::uint32_t nbrNode =
                impl_->nodeIndex.encode(nbrTileIdx, nbrPolyId);
            if (impl_->state.closed.test(nbrNode)) continue;
            const float stepCost = distance(
                impl_->centroids[cur.node], impl_->centroids[nbrNode]);
            const float newG = cur.gCost + stepCost;
            if (newG < impl_->state.gCost[nbrNode]) {
                impl_->state.gCost[nbrNode] = newG;
                impl_->state.cameFrom[nbrNode] = cur.node;
                const float newF =
                    newG + distance(impl_->centroids[nbrNode], req.goal);
                impl_->state.open.push(
                    detail::AStarHeapItem{nbrNode, newG, newF});
            }
        }
    }

    PathResult result;
    result.ready = true;
    result.partial = false;
    result.success = false;
    result.cost = 0.0f;

    std::uint32_t endNode = detail::kInvalidNode;
    if (reachedGoal) {
        result.success = true;
        endNode = goalNode;
    } else if (req.allowPartial) {
        result.success = true;
        result.partial = true;
        endNode = bestNode;
    }

    if (result.success) {
        // Walk cameFrom from end → start, then reverse into corridor order.
        std::vector<std::uint32_t> corridorNodes;
        std::uint32_t n = endNode;
        // Guard against pathological loops by capping at totalNodes.
        std::uint32_t guard = totalNodes + 1u;
        while (n != detail::kInvalidNode && guard > 0u) {
            corridorNodes.push_back(n);
            if (n == startNode) break;
            n = impl_->state.cameFrom[n];
            --guard;
        }
        std::reverse(corridorNodes.begin(), corridorNodes.end());

        result.cost = impl_->state.gCost[endNode];
        if (!std::isfinite(result.cost)) result.cost = 0.0f;

        result.corridor.reserve(corridorNodes.size());
        result.waypoints.reserve(corridorNodes.size() + 1u);
        result.waypoints.push_back(req.start);

        for (std::size_t i = 0; i < corridorNodes.size(); ++i) {
            std::uint32_t curTileIdx;
            NavPolyId curPolyId;
            impl_->nodeIndex.decode(corridorNodes[i], curTileIdx, curPolyId);
            result.corridor.push_back(PathResult::CorridorEntry{
                mesh->tiles()[curTileIdx].id, curPolyId});
            if (i + 1u < corridorNodes.size()) {
                std::uint32_t nextTileIdx;
                NavPolyId nextPolyId;
                impl_->nodeIndex.decode(
                    corridorNodes[i + 1u], nextTileIdx, nextPolyId);
                const std::uint32_t edge = findEdgeTo(
                    *mesh, curTileIdx, curPolyId, nextTileIdx, nextPolyId);
                if (edge != kInvalidPolyIndex) {
                    result.waypoints.push_back(edgeMidpoint(
                        mesh->tiles()[curTileIdx], curPolyId, edge));
                }
            }
        }

        if (reachedGoal) {
            result.waypoints.push_back(req.goal);
        } else {
            // Partial — anchor on the end polygon's centroid so the
            // consumer has a stable hand-off point.
            result.waypoints.push_back(impl_->centroids[endNode]);
        }
    }

    PathId id;
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        id = impl_->nextId++;
        result.id = id;
        impl_->results.emplace(id, std::move(result));
    }
    return id;
}

std::optional<PathResult> PathQueryService::tryGet(PathId id) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    const auto it = impl_->results.find(id);
    if (it == impl_->results.end()) return std::nullopt;
    return it->second;
}

void PathQueryService::cancel(PathId id) {
    std::lock_guard<std::mutex> g(impl_->mtx);
    impl_->results.erase(id);
}

void PathQueryService::clear() {
    std::lock_guard<std::mutex> g(impl_->mtx);
    impl_->results.clear();
}

std::size_t PathQueryService::storedCount() const noexcept {
    std::lock_guard<std::mutex> g(impl_->mtx);
    return impl_->results.size();
}

} // namespace threadmaxx::navmesh
