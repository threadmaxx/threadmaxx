#include "threadmaxx_navmesh/detail/solver.hpp"

#include "threadmaxx_navmesh/detail/a_star.hpp"
#include "threadmaxx_navmesh/detail/funnel.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/query.hpp"

#include "threadmaxx/Components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace threadmaxx::navmesh::detail {

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

bool areaAllowed(std::uint16_t areaTag, std::uint32_t areaMask) noexcept {
    return (areaMask & (std::uint32_t{1} << (areaTag & 31u))) != 0u;
}

/// Find the edge index on `(tileIdx, polyId)` that connects to
/// `(neighborTileIdx, neighborPolyId)`. Returns `kInvalidPolyIndex` if
/// no such adjacency is registered (which would indicate a corridor
/// reconstruction bug, not a runtime error).
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
    scratch.state.open.push(AStarHeapItem{startNode, 0.0f, bestH});

    bool reachedGoal = false;
    while (!scratch.state.open.empty()) {
        const AStarHeapItem cur = scratch.state.open.pop();
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
                    AStarHeapItem{nbrNode, newG, newF});
            }
        }
    }

    std::uint32_t endNode = kInvalidNode;
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
    while (n != kInvalidNode && guard > 0u) {
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
    scratch.portalBuf.push_back(FunnelPortal{req.start, req.start});

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
        scratch.portalBuf.push_back(FunnelPortal{
            tile.vertices[vbIdx], tile.vertices[vaIdx]});
    }

    scratch.portalBuf.push_back(FunnelPortal{endPoint, endPoint});

    stringPullFunnel(scratch.portalBuf, out.waypoints);
}

} // namespace threadmaxx::navmesh::detail
