#include "../Check.hpp"

#include "threadmaxx_navmesh/bake.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace threadmaxx::navmesh;

// N9 — per-triangle area tags must survive the bake. A 3x2 grid of
// quads (split into 12 triangles) with the bottom-middle quad tagged
// area 1 ("water"). With the water mask cleared, A* must detour
// through the top row exactly the way the hand-crafted area-mask-strip
// fixture does in the N3 area test.
//
// Layout (same as `makeAreaMaskStrip` in the N3 fixture, but built
// from triangles + baked instead of hand-written as a blob):
//
//   +-+-+-+      polys (top): quads (0,1) (1,1) (2,1) — all dry
//   |3|4|5|
//   +-+-+-+      polys (bot): quads (0,0) DRY, (1,0) WATER, (2,0) DRY
//   |0|1|2|
//   +-+-+-+
//       x = 0..3, z = 0..2

namespace {

bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

struct GridSoup {
    std::vector<Vec3> vertices;
    std::vector<BakeInputTriangle> triangles;
};

GridSoup buildGrid() {
    GridSoup g;
    // 4x3 vertex grid → 12 verts.
    for (int z = 0; z < 3; ++z) {
        for (int x = 0; x < 4; ++x) {
            g.vertices.push_back(
                Vec3{static_cast<float>(x), 0.0f, static_cast<float>(z)});
        }
    }
    auto vid = [](int x, int z) {
        return static_cast<std::uint32_t>(z * 4 + x);
    };

    // 3x2 quad grid. For each quad split into two CCW triangles. Both
    // triangles inherit the quad's area tag.
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 3; ++x) {
            const std::uint16_t areaTag =
                (x == 1 && z == 0) ? std::uint16_t{1} : std::uint16_t{0};
            const std::uint32_t a = vid(x,     z    );
            const std::uint32_t b = vid(x + 1, z    );
            const std::uint32_t c = vid(x + 1, z + 1);
            const std::uint32_t d = vid(x,     z + 1);
            g.triangles.push_back(BakeInputTriangle{a, b, c, areaTag});
            g.triangles.push_back(BakeInputTriangle{a, c, d, areaTag});
        }
    }
    return g;
}

} // namespace

int main() {
    GridSoup soup = buildGrid();
    CHECK_EQ(soup.vertices.size(), std::size_t{12});
    CHECK_EQ(soup.triangles.size(), std::size_t{12});

    BakeInput in;
    in.vertices = soup.vertices;
    in.triangles = soup.triangles;
    in.name = "bake_areas_strip";
    in.tileId = 0;

    BakeResult baked = bakeNavMesh(in);
    CHECK_EQ(static_cast<int>(baked.error),
             static_cast<int>(BakeError::None));
    if (baked.error != BakeError::None) return gTestFailures;

    NavMeshRegistry reg;
    NavMeshRef ref = reg.load(baked.blob);
    CHECK(reg.isValid(ref));
    if (!reg.isValid(ref)) return gTestFailures;

    // Direct inspection: confirm the area tag landed on every polygon.
    const NavMesh* mesh = reg.find(ref);
    CHECK(mesh != nullptr);
    if (!mesh) return gTestFailures;
    const NavTile* tile = mesh->findTile(NavTileId{0});
    CHECK(tile != nullptr);
    if (!tile) return gTestFailures;
    CHECK_EQ(tile->polygons.size(), std::size_t{12});

    // Tag count: two triangles per quad → exactly 2 polys carry tag=1
    // (the water quad), the other 10 carry tag=0.
    std::size_t waterTris = 0;
    std::size_t dryTris = 0;
    for (const NavPoly& p : tile->polygons) {
        if (p.areaTag == 1) ++waterTris;
        else if (p.areaTag == 0) ++dryTris;
    }
    CHECK_EQ(waterTris, std::size_t{2});
    CHECK_EQ(dryTris, std::size_t{10});

    // -- Query 1: full mask → A* may cross the water quad ---------------
    PathQueryService svc(reg, PathQueryService::Config{0});
    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};
    req.goal  = Vec3{2.5f, 0.0f, 0.5f};
    req.allowPartial = false;

    float fullCost = 0.0f;
    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK(maybe->success);
        CHECK(!maybe->corridor.empty());
        fullCost = maybe->cost;
    }

    // -- Query 2: mask water out → A* MUST detour through the top row --
    // The detour route's centroid-cost is strictly greater than the
    // through-water route's. Confirms the area tag survived the bake
    // AND the runtime honored it.
    req.areaMask = ~std::uint32_t{1u << 1};  // clear bit 1
    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK(maybe->success);
        CHECK(maybe->cost > fullCost);

        // Neither tri of the water quad may appear in the corridor.
        // Build the set of water-poly ids on the fly.
        std::vector<NavPolyId> waterPolys;
        for (std::size_t i = 0; i < tile->polygons.size(); ++i) {
            if (tile->polygons[i].areaTag == 1) {
                waterPolys.push_back(static_cast<NavPolyId>(i));
            }
        }
        for (const auto& entry : maybe->corridor) {
            for (NavPolyId wp : waterPolys) {
                CHECK(!(entry.tileId == NavTileId{0} && entry.polyId == wp));
            }
        }

        // First/last waypoint sanity — N4 invariant.
        CHECK(!maybe->waypoints.empty());
        CHECK(nearly(maybe->waypoints.front().x, req.start.x));
        CHECK(nearly(maybe->waypoints.front().z, req.start.z));
        CHECK(nearly(maybe->waypoints.back().x, req.goal.x));
        CHECK(nearly(maybe->waypoints.back().z, req.goal.z));
    }

    EXIT_WITH_RESULT();
}
