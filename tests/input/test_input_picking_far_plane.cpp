/// @file test_input_picking_far_plane.cpp
/// @brief The returned direction is unit length even at extreme viewport
/// corners — the perspective divide is numerically tight near the far
/// plane.

#include "Check.hpp"

#include <cmath>

#include "picking_test_helpers.hpp"

int main() {
    using namespace threadmaxx::input;

    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float target[3] = {0.0f, 0.0f, 1.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};

    Camera cam{};
    test::lookAt(eye, target, up, cam.view);
    test::perspectiveVulkan(1.5f, 16.0f / 9.0f, 0.01f, 10000.0f, cam.projection);
    cam.viewportX = 0.0f;
    cam.viewportY = 0.0f;
    cam.viewportW = 1920.0f;
    cam.viewportH = 1080.0f;

    const float screenCorners[4][2] = {
        {0.0f, 0.0f},
        {1919.0f, 0.0f},
        {0.0f, 1079.0f},
        {1919.0f, 1079.0f},
    };

    for (const auto& corner : screenCorners) {
        const Ray r = screenToRay(cam, corner[0], corner[1]);
        const float mag = std::sqrt(r.direction[0] * r.direction[0] +
                                    r.direction[1] * r.direction[1] +
                                    r.direction[2] * r.direction[2]);
        CHECK(test::approx(mag, 1.0f, 1e-3f));
    }

    EXIT_WITH_RESULT();
}
