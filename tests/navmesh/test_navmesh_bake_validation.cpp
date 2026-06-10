#include "../Check.hpp"

#include "threadmaxx_navmesh/bake.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <vector>

using namespace threadmaxx::navmesh;

// N9 — exhaustively probe the four BakeError modes. Validation must
// catch every malformed input cleanly (no crash, no asserts) and the
// diagnostic string must be non-empty so callers can surface it.

int main() {
    // -- EmptyInput: zero vertices ------------------------------------------
    {
        BakeInput in;
        in.name = "empty_verts";
        // vertices left empty
        BakeInputTriangle t{0, 0, 0, 0};
        std::vector<BakeInputTriangle> tris{t};
        in.triangles = tris;
        BakeResult r = bakeNavMesh(in);
        CHECK_EQ(static_cast<int>(r.error),
                 static_cast<int>(BakeError::EmptyInput));
        CHECK(!r.diagnostic.empty());
        CHECK(r.blob.empty());
    }

    // -- EmptyInput: zero triangles -----------------------------------------
    {
        BakeInput in;
        in.name = "empty_tris";
        std::vector<Vec3> verts{Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1}};
        in.vertices = verts;
        BakeResult r = bakeNavMesh(in);
        CHECK_EQ(static_cast<int>(r.error),
                 static_cast<int>(BakeError::EmptyInput));
        CHECK(!r.diagnostic.empty());
    }

    // -- InvalidIndex: triangle indexes past the pool -----------------------
    {
        BakeInput in;
        in.name = "oob_index";
        std::vector<Vec3> verts{Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1}};
        in.vertices = verts;
        std::vector<BakeInputTriangle> tris{BakeInputTriangle{0, 1, 5, 0}};
        in.triangles = tris;
        BakeResult r = bakeNavMesh(in);
        CHECK_EQ(static_cast<int>(r.error),
                 static_cast<int>(BakeError::InvalidIndex));
        CHECK(!r.diagnostic.empty());
    }

    // -- DegenerateTriangle: repeated vertex id -----------------------------
    {
        BakeInput in;
        in.name = "repeated_id";
        std::vector<Vec3> verts{Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1}};
        in.vertices = verts;
        std::vector<BakeInputTriangle> tris{BakeInputTriangle{0, 0, 2, 0}};
        in.triangles = tris;
        BakeResult r = bakeNavMesh(in);
        CHECK_EQ(static_cast<int>(r.error),
                 static_cast<int>(BakeError::DegenerateTriangle));
        CHECK(!r.diagnostic.empty());
    }

    // -- DegenerateTriangle: zero XZ area (collinear) -----------------------
    {
        BakeInput in;
        in.name = "collinear";
        std::vector<Vec3> verts{
            Vec3{0,0,0}, Vec3{1,0,0}, Vec3{2,0,0}};  // all on the x axis
        in.vertices = verts;
        std::vector<BakeInputTriangle> tris{BakeInputTriangle{0, 1, 2, 0}};
        in.triangles = tris;
        BakeResult r = bakeNavMesh(in);
        CHECK_EQ(static_cast<int>(r.error),
                 static_cast<int>(BakeError::DegenerateTriangle));
        CHECK(!r.diagnostic.empty());
    }

    // -- NonManifoldEdge: three triangles fanning around a shared edge ------
    //
    // Verts:
    //   0 = (0, 0, 0)
    //   1 = (1, 0, 0)   ← shared edge endpoints (0, 1)
    //   2 = (0, 0, 1)
    //   3 = (0, 0, -1)
    //   4 = (2, 0, 0.5)
    //
    // Triangles:
    //   T0 = (0, 1, 2)  ← edge (0,1) above
    //   T1 = (0, 1, 3)  ← edge (0,1) below — third triangle below
    //   T2 = (1, 0, 4)  ← edge (0,1) third time. Three incidences on
    //                     the same edge → non-manifold.
    {
        BakeInput in;
        in.name = "fan";
        std::vector<Vec3> verts{
            Vec3{0, 0, 0},
            Vec3{1, 0, 0},
            Vec3{0, 0, 1},
            Vec3{0, 0, -1},
            Vec3{2, 0, 0.5f},
        };
        in.vertices = verts;
        std::vector<BakeInputTriangle> tris{
            BakeInputTriangle{0, 1, 2, 0},
            BakeInputTriangle{0, 1, 3, 0},
            BakeInputTriangle{1, 0, 4, 0},
        };
        in.triangles = tris;
        BakeResult r = bakeNavMesh(in);
        CHECK_EQ(static_cast<int>(r.error),
                 static_cast<int>(BakeError::NonManifoldEdge));
        CHECK(!r.diagnostic.empty());
        CHECK(r.blob.empty());
    }

    // -- Happy path: two triangles forming a quad — must succeed -----------
    {
        BakeInput in;
        in.name = "quad";
        std::vector<Vec3> verts{
            Vec3{0, 0, 0}, Vec3{1, 0, 0},
            Vec3{1, 0, 1}, Vec3{0, 0, 1}};
        in.vertices = verts;
        std::vector<BakeInputTriangle> tris{
            BakeInputTriangle{0, 1, 2, 0},
            BakeInputTriangle{0, 2, 3, 0},
        };
        in.triangles = tris;
        BakeResult r = bakeNavMesh(in);
        CHECK_EQ(static_cast<int>(r.error),
                 static_cast<int>(BakeError::None));
        CHECK(r.diagnostic.empty());
        CHECK(!r.blob.empty());
    }

    EXIT_WITH_RESULT();
}
