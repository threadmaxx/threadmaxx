#pragma once

#include "threadmaxx/Components.hpp"

#include <cstddef>
#include <vector>

/// Simple Stupid Funnel Algorithm (Mononen) over a polygon corridor's
/// portal sequence. Given the run of `(left, right)` portal endpoints
/// between consecutive corridor polygons (book-ended by degenerate
/// `{start, start}` and `{end, end}` portals), produces the smoothed
/// waypoint list a follower would walk — start, any corner pinch
/// vertices, end.
///
/// Convention (matches the rest of the library):
///   * Polygons are CCW from above (`+y` up).
///   * `cross2D(a, b, c)` is the standard CCW-positive cross product
///     in the XZ plane.
///   * For an edge `e` of FROM-poly in the corridor, the portal carries
///     `left = v[(e+1) % n]` and `right = v[e]` in vertex-index order
///     so the funnel's left/right line up with a traveler facing the
///     edge from inside FROM-poly.
namespace threadmaxx::navmesh::detail {

using ::threadmaxx::Vec3;

/// Pair of portal endpoints. Degenerate at the start/end (`left == right`).
struct FunnelPortal {
    Vec3 left;
    Vec3 right;
};

/// Standard XZ-plane CCW-positive cross product. Positive ⇔ (a → b → c)
/// is a left turn looking down `+y`.
inline float cross2D(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    return (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
}

inline bool vequalXZ(const Vec3& a, const Vec3& b, float eps = 1e-6f) noexcept {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return (dx * dx + dz * dz) < eps * eps;
}

/// Run the funnel over `portals` and append waypoints to `out`. `out`
/// is `clear()`'d on entry; capacity is preserved across calls so the
/// caller's vector can be reused without reallocating.
inline void stringPullFunnel(const std::vector<FunnelPortal>& portals,
                             std::vector<Vec3>& out) {
    out.clear();
    if (portals.empty()) return;

    Vec3 apex = portals[0].left;        // start: left == right
    Vec3 portalLeft = apex;
    Vec3 portalRight = apex;
    std::size_t apexIdx = 0;
    std::size_t leftIdx = 0;
    std::size_t rightIdx = 0;

    out.push_back(apex);

    std::size_t i = 1;
    while (i < portals.size()) {
        const Vec3& L = portals[i].left;
        const Vec3& R = portals[i].right;

        // Right update: does the new R tighten the right side?
        if (cross2D(apex, portalRight, R) >= 0.0f) {
            if (vequalXZ(apex, portalRight) ||
                cross2D(apex, portalLeft, R) < 0.0f) {
                portalRight = R;
                rightIdx = i;
            } else {
                // Right crossed past left → emit left as the new apex.
                out.push_back(portalLeft);
                apex = portalLeft;
                apexIdx = leftIdx;
                portalLeft = apex;
                portalRight = apex;
                leftIdx = apexIdx;
                rightIdx = apexIdx;
                i = apexIdx + 1;
                continue;
            }
        }

        // Left update: does the new L tighten the left side?
        if (cross2D(apex, portalLeft, L) <= 0.0f) {
            if (vequalXZ(apex, portalLeft) ||
                cross2D(apex, portalRight, L) > 0.0f) {
                portalLeft = L;
                leftIdx = i;
            } else {
                // Left crossed past right → emit right as the new apex.
                out.push_back(portalRight);
                apex = portalRight;
                apexIdx = rightIdx;
                portalLeft = apex;
                portalRight = apex;
                leftIdx = apexIdx;
                rightIdx = apexIdx;
                i = apexIdx + 1;
                continue;
            }
        }

        ++i;
    }

    // Final waypoint: the goal/end (last portal carries left == right).
    // Skip the explicit append if the loop already emitted the goal as
    // a pinch corner — otherwise we'd duplicate. The `size() == 1`
    // guard handles the degenerate same-poly case so callers always
    // get a `[start, end]` shape, even when start == end.
    const Vec3& end = portals.back().left;
    if (out.size() == 1 || !vequalXZ(out.back(), end)) {
        out.push_back(end);
    }
}

} // namespace threadmaxx::navmesh::detail
