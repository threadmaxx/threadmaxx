#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cstdint>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N2 — every interior edge of the L-shape fixture has a reciprocal
// portal, and every outer-boundary edge points at no neighbor (either
// intra-tile via `kInvalidPolyIndex` or cross-tile via no portal entry).

int main() {
    NavMeshRegistry reg;
    auto blob = make4TileLShape();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));
    CHECK(reg.lastLoadError() == NavMeshLoadError::None);

    const NavMesh* mesh = reg.find(ref);
    CHECK(mesh != nullptr);
    CHECK_EQ(mesh->tiles().size(), std::size_t{4});

    // 4 tiles * 4 polys = 16 polygons; vertex pools are per-tile.
    CHECK_EQ(mesh->polygonCount(), std::uint32_t{16});
    CHECK_EQ(mesh->vertexCount(), std::uint32_t{4 * 9});

    // The fixture wires 6 cross-tile portals (2 per shared edge).
    CHECK_EQ(mesh->portals().size(), std::size_t{6});

    // Every portal must be reciprocal — querying from either side should
    // return the matching opposite triple. This catches a load-time bug
    // where the per-tile portal index lost the B-side mapping.
    for (const NavPortal& p : mesh->portals()) {
        auto fromA = mesh->crossTileNeighbor(p.tileA, p.polyA, p.edgeA);
        CHECK(fromA.has_value());
        CHECK_EQ(fromA->tileId, p.tileB);
        CHECK_EQ(fromA->polyId, p.polyB);
        CHECK_EQ(fromA->edgeIdx, p.edgeB);

        auto fromB = mesh->crossTileNeighbor(p.tileB, p.polyB, p.edgeB);
        CHECK(fromB.has_value());
        CHECK_EQ(fromB->tileId, p.tileA);
        CHECK_EQ(fromB->polyId, p.polyA);
        CHECK_EQ(fromB->edgeIdx, p.edgeA);
    }

    // `portalsForTile` should observe both sides of each portal.
    //  T0 touches T1 and T2 → 4 portals (2 to T1, 2 to T2).
    //  T1 touches T0 and T3 → 4 portals.
    //  T2 touches only T0   → 2 portals.
    //  T3 touches only T1   → 2 portals.
    CHECK_EQ(mesh->portalsForTile(0).size(), std::size_t{4});
    CHECK_EQ(mesh->portalsForTile(1).size(), std::size_t{4});
    CHECK_EQ(mesh->portalsForTile(2).size(), std::size_t{2});
    CHECK_EQ(mesh->portalsForTile(3).size(), std::size_t{2});

    // Boundary-edge sanity: T0's poly (0,0) sits at the L-shape's
    // outside corner. Its -z (edge 0) and -x (edge 3) edges face open
    // space — they must read `kInvalidPolyIndex` intra-tile AND not
    // resolve to a portal.
    const NavTile* t0 = mesh->findTile(0);
    CHECK(t0 != nullptr);
    const NavPoly& p00 = t0->polygons[0];
    CHECK_EQ(t0->neighborPolys[p00.indexStart + 0], kInvalidPolyIndex);
    CHECK_EQ(t0->neighborPolys[p00.indexStart + 3], kInvalidPolyIndex);
    CHECK(!mesh->crossTileNeighbor(0, /*polyId=*/0, /*edge=*/0).has_value());
    CHECK(!mesh->crossTileNeighbor(0, /*polyId=*/0, /*edge=*/3).has_value());

    // T3's poly (1,0) sits at the L's far-bottom-right corner. Its +x
    // (edge 1) and -z (edge 0) edges are L outer edges as well.
    const NavTile* t3 = mesh->findTile(3);
    CHECK(t3 != nullptr);
    const NavPoly& p10_t3 = t3->polygons[1];
    CHECK_EQ(t3->neighborPolys[p10_t3.indexStart + 0], kInvalidPolyIndex);
    CHECK_EQ(t3->neighborPolys[p10_t3.indexStart + 1], kInvalidPolyIndex);
    CHECK(!mesh->crossTileNeighbor(3, /*polyId=*/1, /*edge=*/0).has_value());
    CHECK(!mesh->crossTileNeighbor(3, /*polyId=*/1, /*edge=*/1).has_value());

    // A polygon that DOES sit on an inner (cross-tile) edge resolves to
    // a portal — confirm one explicit case: T0 poly (1,0)=1 edge 1 (+x)
    // crosses into T1 poly (0,0)=0 edge 3 (-x).
    const NavPoly& p10_t0 = t0->polygons[1];
    CHECK_EQ(t0->neighborPolys[p10_t0.indexStart + 1], kInvalidPolyIndex);  // tile-boundary on the intra-tile table
    auto xtile = mesh->crossTileNeighbor(0, /*polyId=*/1, /*edge=*/1);
    CHECK(xtile.has_value());
    CHECK_EQ(xtile->tileId, NavTileId{1});
    CHECK_EQ(xtile->polyId, NavPolyId{0});
    CHECK_EQ(xtile->edgeIdx, std::uint32_t{3});

    // Unknown tile-id lookups are graceful no-ops.
    CHECK_EQ(mesh->portalsForTile(99).size(), std::size_t{0});
    CHECK(!mesh->crossTileNeighbor(99, 0, 0).has_value());

    EXIT_WITH_RESULT();
}
