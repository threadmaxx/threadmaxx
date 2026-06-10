#include "../Check.hpp"

#include "threadmaxx_navmesh/detail/funnel.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

using threadmaxx::Vec3;
using threadmaxx::navmesh::detail::FunnelPortal;
using threadmaxx::navmesh::detail::stringPullFunnel;

// N4 — Simple Stupid Funnel correctness on a hand-built L-shaped
// corridor. The portal sequence describes a 5-poly L (going +z then +x);
// the corridor walks two unit-square polys north, makes a right turn at
// the inner corner (1, 0, 1), then walks two more unit-square polys
// east. The funnel must pinch exactly once at (1, 0, 1) and emit
// `[start, (1,0,1), goal]`.
//
// Tested directly on `stringPullFunnel` so the assertion is independent
// of A*'s tiebreaker — the integrated path-query happy path is covered
// by `test_navmesh_funnel_pinch` and the N3 corridor tests.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    // Polygon corridor (each poly is a unit-square in CCW-from-above
    // vertex order). Edges named by their portal-into-next-poly.
    //
    //     z=3 +-----+-----+-----+
    //         |  P4 |     |     |
    //     z=2 +-----+-----+-----+
    //         |  P2 |     |     |
    //     z=1 +-----+-----+-----+
    //         |  P0 |  P3 |     |
    //     z=0 +-----+-----+
    //         x=0   x=1   x=2   x=3
    //
    // Corridor: P0 (south start) → P1 (+z) → P2 (+z) → P3 (+x of P2)
    //          → P4 (+x of P3). NOTE we use the L's bend at the (2,2)
    // inner corner. Wait — the diagram conflicts; let me redo.
    //
    // Concrete corridor for this test: 3 polys forming an L.
    //     P0 = quad {(0,0), (1,0), (1,1), (0,1)}  [south leg]
    //     P1 = quad {(0,1), (1,1), (1,2), (0,2)}  [north of P0]
    //     P2 = quad {(1,1), (2,1), (2,2), (1,2)}  [east of P1]
    //
    // The shared edges (LEFT = v[(e+1)%n], RIGHT = v[e]):
    //   P0→P1: edge 2 of P0, vertices v[2]=(1,1), v[3]=(0,1).
    //          LEFT  = v[3] = (0, 1)
    //          RIGHT = v[2] = (1, 1)
    //   P1→P2: edge 1 of P1, vertices v[1]=(1,1), v[2]=(1,2).
    //          LEFT  = v[2] = (1, 2)
    //          RIGHT = v[1] = (1, 1)
    //
    // start sits in the lower-left of P0; goal in the upper-right of P2.
    // The funnel pinches against the (1, 0, 1) inner corner.

    // Goal is intentionally far enough east that the straight line from
    // `start` exits the corridor before reaching portal_2 — that forces
    // the funnel to pinch against (1, 0, 1). A perfectly symmetric setup
    // (goal at (1.5, 1.5)) would let the straight line graze the
    // corner point and collapse to 2 waypoints.
    const Vec3 start{0.5f, 0.0f, 0.5f};
    const Vec3 goal {2.5f, 0.0f, 1.5f};

    std::vector<FunnelPortal> portals;
    portals.push_back(FunnelPortal{start, start});
    portals.push_back(FunnelPortal{Vec3{0.0f, 0.0f, 1.0f},
                                   Vec3{1.0f, 0.0f, 1.0f}});
    portals.push_back(FunnelPortal{Vec3{1.0f, 0.0f, 2.0f},
                                   Vec3{1.0f, 0.0f, 1.0f}});
    portals.push_back(FunnelPortal{goal, goal});

    std::vector<Vec3> out;
    stringPullFunnel(portals, out);

    CHECK_EQ(out.size(), std::size_t{3});
    CHECK(nearly(out.front().x, start.x));
    CHECK(nearly(out.front().z, start.z));
    CHECK(nearly(out[1].x, 1.0f));
    CHECK(nearly(out[1].z, 1.0f));
    CHECK(nearly(out.back().x, goal.x));
    CHECK(nearly(out.back().z, goal.z));

    // Straight-line subcase: collinear portals produce 2 waypoints.
    std::vector<FunnelPortal> straight;
    straight.push_back(FunnelPortal{Vec3{0.5f, 0, 0.5f}, Vec3{0.5f, 0, 0.5f}});
    straight.push_back(FunnelPortal{Vec3{0, 0, 1}, Vec3{1, 0, 1}});
    straight.push_back(FunnelPortal{Vec3{0, 0, 2}, Vec3{1, 0, 2}});
    straight.push_back(FunnelPortal{Vec3{0.5f, 0, 2.5f}, Vec3{0.5f, 0, 2.5f}});

    std::vector<Vec3> straightOut;
    stringPullFunnel(straight, straightOut);
    CHECK_EQ(straightOut.size(), std::size_t{2});

    EXIT_WITH_RESULT();
}
