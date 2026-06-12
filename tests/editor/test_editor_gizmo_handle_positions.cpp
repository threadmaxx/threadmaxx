/// @file test_editor_gizmo_handle_positions.cpp
/// @brief E8 — gizmo for an identity-transform entity returns 3 axis
/// handles at unit positions in X / Y / Z.

#include "Check.hpp"

#include <threadmaxx_editor/gizmo.hpp>

int main() {
    threadmaxx::editor::TranslateGizmo giz;
    auto frame = giz.frameFor({0.0f, 0.0f, 0.0f});
    CHECK(frame.mode == threadmaxx::editor::GizmoMode::Translate);
    CHECK(frame.x.axis == threadmaxx::editor::GizmoAxis::X);
    CHECK_EQ(frame.x.axisDir.x, 1.0f);
    CHECK_EQ(frame.x.axisDir.y, 0.0f);
    CHECK_EQ(frame.x.axisDir.z, 0.0f);
    CHECK_EQ(frame.x.length, 1.0f);

    CHECK(frame.y.axis == threadmaxx::editor::GizmoAxis::Y);
    CHECK_EQ(frame.y.axisDir.y, 1.0f);

    CHECK(frame.z.axis == threadmaxx::editor::GizmoAxis::Z);
    CHECK_EQ(frame.z.axisDir.z, 1.0f);

    // Anchored at the entity position.
    auto shifted = giz.frameFor({5.0f, -2.0f, 3.0f});
    CHECK_EQ(shifted.origin.x, 5.0f);
    CHECK_EQ(shifted.x.origin.x, 5.0f);
    CHECK_EQ(shifted.y.origin.y, -2.0f);
    CHECK_EQ(shifted.z.origin.z, 3.0f);

    EXIT_WITH_RESULT();
}
