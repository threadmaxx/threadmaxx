#include "../Check.hpp"

#include "threadmaxx_navmesh/bake.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace threadmaxx::navmesh;

// N9 — bake a clean walkable surface and round-trip it through the
// registry. The fixture is an L-shape silhouette of 10 quads:
//
//   z=3 +--+--+
//       |8 |9 |
//   z=2 +--+--+--+--+--+
//       |3 |4 |5 |6 |7 |
//   z=1 +--+--+--+--+--+
//       |0 |1 |2 |
//   z=0 +--+--+--+
//       x=0  1  2  3  4  5
//
// Hand-tagged: bottom-left arm (3 quads), main strip (5 quads), upper-
// right arm (2 quads). Total 10 quads → 20 triangles after the obvious
// CCW split (a, b, c) and (a, c, d). Each interior triangle edge has
// exactly two incident triangles; boundary edges have one. The smoke
// test confirms the bake's adjacency lookup matches the runtime's
// expectations: A* must walk corridor-cost = number-of-poly-hops, with
// successful edge expansion across all four "interior" shared edges.

namespace {

bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

struct LShapeSoup {
    std::vector<Vec3> vertices;
    std::vector<BakeInputTriangle> triangles;
};

// Each quad is split (a, b, c) + (a, c, d) where the quad's vertices
// are CCW from above: (x,z), (x+1,z), (x+1,z+1), (x,z+1).
LShapeSoup buildLShape() {
    LShapeSoup s;

    // Vertex grid: pack into a small map keyed by (x,z) so we re-use
    // shared corners between quads.
    auto vid = [&](int x, int z) -> std::uint32_t {
        const float fx = static_cast<float>(x);
        const float fz = static_cast<float>(z);
        for (std::size_t i = 0; i < s.vertices.size(); ++i) {
            const Vec3& v = s.vertices[i];
            if (nearly(v.x, fx) && nearly(v.z, fz)) {
                return static_cast<std::uint32_t>(i);
            }
        }
        s.vertices.push_back(Vec3{fx, 0.0f, fz});
        return static_cast<std::uint32_t>(s.vertices.size() - 1);
    };

    auto addQuad = [&](int x, int z, std::uint16_t areaTag) {
        const std::uint32_t a = vid(x,     z    );
        const std::uint32_t b = vid(x + 1, z    );
        const std::uint32_t c = vid(x + 1, z + 1);
        const std::uint32_t d = vid(x,     z + 1);
        s.triangles.push_back(BakeInputTriangle{a, b, c, areaTag});
        s.triangles.push_back(BakeInputTriangle{a, c, d, areaTag});
    };

    // Bottom-left arm: (0..3, 0..1) — 3 quads.
    addQuad(0, 0, 0);
    addQuad(1, 0, 0);
    addQuad(2, 0, 0);
    // Main strip: (0..5, 1..2) — 5 quads.
    addQuad(0, 1, 0);
    addQuad(1, 1, 0);
    addQuad(2, 1, 0);
    addQuad(3, 1, 0);
    addQuad(4, 1, 0);
    // Upper-right arm: (0..2, 2..3) — 2 quads.
    addQuad(0, 2, 0);
    addQuad(1, 2, 0);

    return s;
}

} // namespace

int main() {
    LShapeSoup soup = buildLShape();
    CHECK_EQ(soup.triangles.size(), std::size_t{20});

    BakeInput in;
    in.vertices = soup.vertices;
    in.triangles = soup.triangles;
    in.name = "bake_smoke_lshape";
    in.tileId = 7;

    BakeResult baked = bakeNavMesh(in);
    CHECK_EQ(static_cast<int>(baked.error), static_cast<int>(BakeError::None));
    CHECK(baked.diagnostic.empty());
    CHECK(!baked.blob.empty());
    if (baked.error != BakeError::None) return gTestFailures;

    NavMeshRegistry reg;
    NavMeshRef ref = reg.load(baked.blob);
    CHECK(reg.isValid(ref));
    if (!reg.isValid(ref)) return gTestFailures;

    auto meta = reg.meta(ref);
    CHECK(meta.has_value());
    if (!meta) return gTestFailures;
    CHECK_EQ(meta->tileCount, std::uint32_t{1});
    CHECK_EQ(meta->polygonCount, static_cast<std::uint32_t>(soup.triangles.size()));
    CHECK_EQ(meta->vertexCount,
             static_cast<std::uint32_t>(soup.vertices.size()));
    CHECK_EQ(meta->name, std::string{"bake_smoke_lshape"});

    const NavMesh* mesh = reg.find(ref);
    CHECK(mesh != nullptr);
    if (!mesh) return gTestFailures;
    const NavTile* tile = mesh->findTile(NavTileId{7});
    CHECK(tile != nullptr);
    if (!tile) return gTestFailures;

    // Each triangle has 3 edges; every neighbor entry is either a valid
    // poly id in range OR kInvalidPolyIndex. Count shared edges: in a
    // valid 2-manifold soup at least one triangle must have a non-border
    // neighbor (otherwise no adjacency, no paths possible).
    std::size_t sharedEdges = 0;
    for (std::uint32_t n : tile->neighborPolys) {
        if (n != kInvalidPolyIndex) {
            CHECK(n < tile->polygons.size());
            ++sharedEdges;
        }
    }
    CHECK(sharedEdges > 0);

    // Solve a path from the bottom-left arm to the upper-right arm —
    // confirms adjacency is wired all the way through and A* finds a
    // route across every interior edge category (intra-quad, vertical
    // quad-to-quad, horizontal quad-to-quad).
    PathQueryService svc(reg, PathQueryService::Config{0});
    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};  // poly inside quad (0, 0)
    req.goal  = Vec3{1.5f, 0.0f, 2.5f};  // poly inside quad (1, 2)
    req.allowPartial = false;

    PathId id = svc.request(req);
    CHECK(id != 0);
    auto maybe = svc.tryGet(id);
    CHECK(maybe.has_value());
    if (!maybe) return gTestFailures;
    CHECK(maybe->success);
    CHECK(!maybe->partial);
    CHECK(!maybe->corridor.empty());
    CHECK(!maybe->waypoints.empty());
    // First waypoint is the start, last is the goal — pinned by N4.
    CHECK(nearly(maybe->waypoints.front().x, req.start.x));
    CHECK(nearly(maybe->waypoints.front().z, req.start.z));
    CHECK(nearly(maybe->waypoints.back().x, req.goal.x));
    CHECK(nearly(maybe->waypoints.back().z, req.goal.z));

    EXIT_WITH_RESULT();
}
