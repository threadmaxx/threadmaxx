/// @file test_input_picking_identity_camera.cpp
/// @brief Identity view + identity projection + screen center yields a ray
/// at origin (0, 0, 0) pointing along +Z (Vulkan NDC near→far direction).

#include "Check.hpp"

#include "picking_test_helpers.hpp"

int main() {
    using namespace threadmaxx::input;

    Camera cam{};
    test::identity4(cam.view);
    test::identity4(cam.projection);
    cam.viewportX = 0.0f;
    cam.viewportY = 0.0f;
    cam.viewportW = 800.0f;
    cam.viewportH = 600.0f;

    const Ray r = screenToRay(cam, 400.0f, 300.0f);

    // Origin at NDC near (0, 0, 0) → world (0, 0, 0) under identity.
    CHECK(test::approx(r.origin[0], 0.0f));
    CHECK(test::approx(r.origin[1], 0.0f));
    CHECK(test::approx(r.origin[2], 0.0f));

    // Direction: near (0,0,0) → far (0,0,1) under identity, normalized = (0,0,1).
    CHECK(test::approx(r.direction[0], 0.0f));
    CHECK(test::approx(r.direction[1], 0.0f));
    CHECK(test::approx(r.direction[2], 1.0f));

    EXIT_WITH_RESULT();
}
