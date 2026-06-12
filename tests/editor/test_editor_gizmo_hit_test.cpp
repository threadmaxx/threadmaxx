/// @file test_editor_gizmo_hit_test.cpp
/// @brief E8 — a ray pointed at an axis handle picks the right axis;
/// a ray that misses returns GizmoAxis::None.

#include "Check.hpp"

#include <threadmaxx_editor/gizmo.hpp>

int main() {
    threadmaxx::editor::TranslateGizmo giz;
    const auto frame = giz.frameFor({0.0f, 0.0f, 0.0f});

    // Ray from -Z aimed straight at the middle of the X handle.
    threadmaxx::editor::Ray3 rayX{};
    rayX.origin = {0.5f, 0.0f, -2.0f};
    rayX.dir    = {0.0f, 0.0f, 1.0f};
    CHECK(giz.hitTest(frame, rayX) == threadmaxx::editor::GizmoAxis::X);

    // Ray from -X aimed at the Y handle midpoint.
    threadmaxx::editor::Ray3 rayY{};
    rayY.origin = {-2.0f, 0.5f, 0.0f};
    rayY.dir    = {1.0f, 0.0f, 0.0f};
    CHECK(giz.hitTest(frame, rayY) == threadmaxx::editor::GizmoAxis::Y);

    // Ray from -X aimed at the Z handle midpoint.
    threadmaxx::editor::Ray3 rayZ{};
    rayZ.origin = {-2.0f, 0.0f, 0.5f};
    rayZ.dir    = {1.0f, 0.0f, 0.0f};
    CHECK(giz.hitTest(frame, rayZ) == threadmaxx::editor::GizmoAxis::Z);

    // Way off to the side — none picked.
    threadmaxx::editor::Ray3 miss{};
    miss.origin = {10.0f, 10.0f, -2.0f};
    miss.dir    = {0.0f, 0.0f, 1.0f};
    CHECK(giz.hitTest(frame, miss) == threadmaxx::editor::GizmoAxis::None);

    EXIT_WITH_RESULT();
}
