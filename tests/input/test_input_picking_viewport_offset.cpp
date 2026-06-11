/// @file test_input_picking_viewport_offset.cpp
/// @brief A viewport with a non-zero origin (e.g. an editor pane) does
/// not contaminate the ray. The center of the viewport (in screen space)
/// produces the same world-space ray regardless of the offset.

#include "Check.hpp"

#include "picking_test_helpers.hpp"

int main() {
    using namespace threadmaxx::input;

    Camera at_origin{};
    test::identity4(at_origin.view);
    test::perspectiveVulkan(1.0f, 16.0f / 9.0f, 0.1f, 100.0f, at_origin.projection);
    at_origin.viewportX = 0.0f;
    at_origin.viewportY = 0.0f;
    at_origin.viewportW = 1600.0f;
    at_origin.viewportH = 900.0f;

    Camera offset = at_origin;
    offset.viewportX = 400.0f;
    offset.viewportY = 300.0f;

    // Center of each viewport.
    const Ray rA = screenToRay(at_origin, 800.0f, 450.0f);
    const Ray rB = screenToRay(offset, 400.0f + 800.0f, 300.0f + 450.0f);

    CHECK(test::approx(rA.origin[0], rB.origin[0]));
    CHECK(test::approx(rA.origin[1], rB.origin[1]));
    CHECK(test::approx(rA.origin[2], rB.origin[2]));
    CHECK(test::approx(rA.direction[0], rB.direction[0]));
    CHECK(test::approx(rA.direction[1], rB.direction[1]));
    CHECK(test::approx(rA.direction[2], rB.direction[2]));

    EXIT_WITH_RESULT();
}
