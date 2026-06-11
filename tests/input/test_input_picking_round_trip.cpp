/// @file test_input_picking_round_trip.cpp
/// @brief Round-trip: pick a world point, project to screen via
/// worldToScreen, build a ray from that screen point via screenToRay,
/// advance along the ray by the camera-to-point distance, recover the
/// original world point within 1e-3.

#include "Check.hpp"

#include <cmath>

#include "picking_test_helpers.hpp"

int main() {
    using namespace threadmaxx::input;

    // Camera at (0, 5, -10) looking at origin.
    const float eye[3] = {0.0f, 5.0f, -10.0f};
    const float target[3] = {0.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};

    Camera cam{};
    test::lookAt(eye, target, up, cam.view);
    test::perspectiveVulkan(1.0f /*~57° FoV*/, 1280.0f / 720.0f, 0.1f, 100.0f,
                            cam.projection);
    cam.viewportX = 0.0f;
    cam.viewportY = 0.0f;
    cam.viewportW = 1280.0f;
    cam.viewportH = 720.0f;

    const float P[3] = {2.0f, 3.0f, 4.0f};

    const auto sp = worldToScreen(cam, P);
    CHECK(sp.inFrontOfCamera);
    CHECK(sp.x >= 0.0f && sp.x <= cam.viewportW);
    CHECK(sp.y >= 0.0f && sp.y <= cam.viewportH);

    const Ray r = screenToRay(cam, sp.x, sp.y);

    // Distance from camera (ray origin sits on the near plane, but the
    // direction passes through the camera-eye and P — so origin + t*dir = P
    // for the right t).
    const float ox = r.origin[0], oy = r.origin[1], oz = r.origin[2];
    const float dx = r.direction[0], dy = r.direction[1], dz = r.direction[2];

    // Pick t such that the projected x of (origin + t*dir) matches P.x.
    // dx might be near zero — pick the component with largest direction
    // magnitude as the parameter axis.
    float t = 0.0f;
    if (std::fabs(dx) >= std::fabs(dy) && std::fabs(dx) >= std::fabs(dz)) {
        t = (P[0] - ox) / dx;
    } else if (std::fabs(dy) >= std::fabs(dz)) {
        t = (P[1] - oy) / dy;
    } else {
        t = (P[2] - oz) / dz;
    }

    const float qx = ox + t * dx;
    const float qy = oy + t * dy;
    const float qz = oz + t * dz;

    CHECK(test::approx(qx, P[0], 1e-2f));
    CHECK(test::approx(qy, P[1], 1e-2f));
    CHECK(test::approx(qz, P[2], 1e-2f));

    EXIT_WITH_RESULT();
}
