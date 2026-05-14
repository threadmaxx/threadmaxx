// §3.2 batch 8: frustum extraction + AABB-vs-frustum + per-camera mask
// population from cullByFrustum.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cmath>
#include <vector>

namespace {

// Build a basic perspective projection matrix (column-major) and an
// identity view so we know the frustum's world-space shape.
threadmaxx::Camera makeForwardCamera() {
    threadmaxx::Camera c;
    c.mode = threadmaxx::ProjectionMode::Perspective;
    c.nearZ = 0.1f;
    c.farZ = 100.0f;
    c.fovYRadians = 1.5708f;  // 90 degrees
    c.aspect = 1.0f;

    // GL-style column-major perspective.
    const float f = 1.0f / std::tan(c.fovYRadians * 0.5f);
    const float nf = 1.0f / (c.nearZ - c.farZ);
    c.projection = {
        f / c.aspect, 0,  0,                       0,
        0,            f,  0,                       0,
        0,            0,  (c.farZ + c.nearZ) * nf, -1,
        0,            0,  2 * c.farZ * c.nearZ * nf, 0,
    };
    // Identity view (camera at origin looking down -Z).
    c.view = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    return c;
}

} // namespace

int main() {
    using namespace threadmaxx;

    Camera cam = makeForwardCamera();

    // An AABB straight in front of the camera should be visible.
    CHECK(intersectsFrustum(cam, Vec3{-1, -1, -10}, Vec3{1, 1, -5}));

    // An AABB behind the camera should NOT be visible.
    CHECK(!intersectsFrustum(cam, Vec3{-1, -1, 1}, Vec3{1, 1, 5}));

    // Far away beyond farZ — out of frustum.
    CHECK(!intersectsFrustum(cam, Vec3{-1, -1, -200}, Vec3{1, 1, -150}));

    // cullByFrustum populates DrawItem::cameraMask in parallel.
    std::vector<DrawItem> items(3);
    std::vector<BoundingVolume> bounds(3);
    bounds[0] = {{-1, -1, -10}, {1, 1, -5}};   // visible
    bounds[1] = {{-1, -1, 1},   {1, 1, 5}};    // behind camera
    bounds[2] = {{-1, -1, -10}, {1, 1, -5}};   // visible

    std::vector<Camera> cameras{cam};
    cullByFrustum(items, bounds, cameras);

    CHECK_EQ(items[0].cameraMask, 1u);  // bit 0 = camera 0 visible
    CHECK_EQ(items[1].cameraMask, 0u);
    CHECK_EQ(items[2].cameraMask, 1u);

    EXIT_WITH_RESULT();
}
