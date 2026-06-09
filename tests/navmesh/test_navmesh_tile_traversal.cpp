#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/detail/bitset.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cstdint>
#include <vector>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N2 — a hand-written BFS walker that uses both intra-tile neighbors
// and cross-tile portals must visit every polygon across all four tiles
// of the L-shape fixture, starting from any seed polygon.

namespace {

struct Node {
    NavTileId tile;
    NavPolyId poly;
    constexpr bool operator==(const Node&) const noexcept = default;
};

/// Walk the navmesh from `seed` and return the count of distinct
/// `(tile, poly)` nodes reached. Uses a bitset keyed by
/// `tileIndex * kMaxPolysPerTile + polyId` so we can stop revisits in
/// O(1).
std::size_t reachableNodes(const NavMesh& mesh, Node seed) {
    constexpr std::size_t kMaxPolysPerTile = 64;  // fixture-sized.
    detail::Bitset visited(mesh.tiles().size() * kMaxPolysPerTile);

    auto encode = [&](const NavMesh& m, Node n) -> std::size_t {
        const auto idx = m.tileIndex(n.tile);
        return *idx * kMaxPolysPerTile + n.poly;
    };

    std::vector<Node> frontier;
    frontier.push_back(seed);
    std::size_t reached = 0;

    while (!frontier.empty()) {
        Node cur = frontier.back();
        frontier.pop_back();

        if (visited.testAndSet(encode(mesh, cur))) continue;
        ++reached;

        const NavTile* tile = mesh.findTile(cur.tile);
        if (!tile) continue;
        const NavPoly& poly = tile->polygons[cur.poly];

        // Walk each edge of the polygon.
        for (std::uint32_t e = 0; e < poly.indexCount; ++e) {
            // Intra-tile neighbor first.
            const std::uint32_t intra =
                tile->neighborPolys[poly.indexStart + e];
            if (intra != kInvalidPolyIndex) {
                frontier.push_back(Node{cur.tile, intra});
                continue;
            }
            // Otherwise see if the edge is a portal into a sibling tile.
            auto cross = mesh.crossTileNeighbor(cur.tile, cur.poly, e);
            if (cross) {
                frontier.push_back(Node{cross->tileId, cross->polyId});
            }
        }
    }
    return reached;
}

} // namespace

int main() {
    NavMeshRegistry reg;
    auto blob = make4TileLShape();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));
    const NavMesh* mesh = reg.find(ref);
    CHECK(mesh != nullptr);

    constexpr std::size_t kExpected = 16;  // 4 tiles * 4 polys.

    // Seeding from every (tile, poly) should reach every other node.
    for (const NavTile& tile : mesh->tiles()) {
        for (NavPolyId p = 0;
             p < static_cast<NavPolyId>(tile.polygons.size()); ++p) {
            const std::size_t hit =
                reachableNodes(*mesh, Node{tile.id, p});
            CHECK_EQ(hit, kExpected);
        }
    }

    EXIT_WITH_RESULT();
}
