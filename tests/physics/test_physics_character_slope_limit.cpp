#include "Check.hpp"

#include "threadmaxx_physics/character.hpp"

#include <cmath>

// P7 — `isSurfaceWalkable` gates slope walkability at the
// `desc.slopeLimit` boundary. The stub backend's queries are
// AABB-only and produce only axis-aligned hit normals, so the slope
// check is exercised here at the function level with synthetic
// normals. Game code reads ground normals from the backend's
// `raycast` / `sweep` hits (real backends — P9) and feeds them to
// this helper to gate jump / slide / momentum decisions.
//
// Properties verified:
//   1. Flat ground (normal == world-up) is walkable for any
//      slopeLimit > 0.
//   2. Vertical wall (normal == world-X / -X / Z / -Z) is unwalkable
//      for any slopeLimit < π/2.
//   3. A slope whose angle from world-up sits inside `slopeLimit` is
//      walkable; outside it is not.
//   4. Boundary: a slope at exactly `slopeLimit` is walkable (cos
//      check uses `>=`).
//   5. Degenerate limits: `slopeLimit <= 0` rejects everything;
//      `slopeLimit >= π/2` accepts every upward-facing surface.

using namespace threadmaxx::physics;

namespace {

// Build a unit normal tilted `angleRad` from world-up, leaning in +X.
inline Vec3 normalAtAngle(float angleRad) noexcept {
    return Vec3{std::sin(angleRad), std::cos(angleRad), 0.0f};
}

} // namespace

int main() {
    CharacterControllerDesc desc;
    desc.slopeLimit = 0.5236f;  // ~30°

    // 1. Flat ground always walkable.
    CHECK(isSurfaceWalkable(desc, Vec3{0.0f, 1.0f, 0.0f}));

    // 2. Vertical wall never walkable.
    CHECK(!isSurfaceWalkable(desc, Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(!isSurfaceWalkable(desc, Vec3{-1.0f, 0.0f, 0.0f}));
    CHECK(!isSurfaceWalkable(desc, Vec3{0.0f, 0.0f, 1.0f}));
    CHECK(!isSurfaceWalkable(desc, Vec3{0.0f, 0.0f, -1.0f}));

    // 3a. Slope strictly under the limit: 15° < 30° → walkable.
    CHECK(isSurfaceWalkable(desc, normalAtAngle(0.2618f)));  // ~15°
    // 3b. Slope strictly over the limit: 45° > 30° → unwalkable.
    CHECK(!isSurfaceWalkable(desc, normalAtAngle(0.7854f)));  // ~45°
    // 3c. Just-under: 29° → walkable.
    CHECK(isSurfaceWalkable(desc, normalAtAngle(0.5061f)));   // ~29°
    // 3d. Just-over: 31° → unwalkable.
    CHECK(!isSurfaceWalkable(desc, normalAtAngle(0.5411f)));  // ~31°

    // 4. Boundary: slope at exactly `slopeLimit` should be walkable
    //    (the helper uses `>=`, not `>`). Float roundoff means we
    //    test at `slopeLimit - tiny` to be safe.
    CHECK(isSurfaceWalkable(desc, normalAtAngle(desc.slopeLimit - 1.0e-5f)));

    // 5a. Degenerate: slopeLimit == 0 → nothing walkable, even flat
    //     ground.
    CharacterControllerDesc zero;
    zero.slopeLimit = 0.0f;
    CHECK(!isSurfaceWalkable(zero, Vec3{0.0f, 1.0f, 0.0f}));

    // 5b. slopeLimit < 0 → same.
    CharacterControllerDesc neg;
    neg.slopeLimit = -0.1f;
    CHECK(!isSurfaceWalkable(neg, Vec3{0.0f, 1.0f, 0.0f}));

    // 5c. slopeLimit >= π/2 → every upward-facing surface walkable,
    //     including grazing slopes near vertical.
    CharacterControllerDesc wide;
    wide.slopeLimit = 1.5708f;  // 90°
    CHECK(isSurfaceWalkable(wide, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(isSurfaceWalkable(wide, normalAtAngle(1.5707f)));  // 89.99°
    // Downward-facing normal (y < 0) never walkable under any limit.
    CHECK(!isSurfaceWalkable(wide, Vec3{0.0f, -1.0f, 0.0f}));

    // 6. Verify the helper composes with a custom slopeLimit driven
    //    via desc (independent of default).
    CharacterControllerDesc steep;
    steep.slopeLimit = 1.0472f;  // ~60°
    CHECK(isSurfaceWalkable(steep, normalAtAngle(1.0f)));   // ~57°
    CHECK(!isSurfaceWalkable(steep, normalAtAngle(1.1f)));  // ~63°

    EXIT_WITH_RESULT();
}
