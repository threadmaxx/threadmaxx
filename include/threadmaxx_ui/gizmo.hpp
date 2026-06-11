#pragma once

/// @file gizmo.hpp
/// @brief Screen-space 2D drag handles. Pure 2D — 3D world gizmos (axis
/// translate / rotate / scale against a camera ray) are editor-side, NOT
/// shipped here.
///
/// Use cases:
///   - resize corners of a panel / box.
///   - keyframe handles in a 2D animation timeline.
///   - vertex grab on a 2D level layout.

#include <cstdint>

#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class UIContext;

/// One frame's gizmo outcome.
struct GizmoEvent {
    bool hovered  = false;
    bool active   = false;
    bool dragging = false;   // active AND mouse moved this frame
    Vec2i delta{};           // per-frame mouse delta while active
};

/// Square drag handle at `bounds`. When the user mouse-presses inside and
/// drags, the per-frame delta is reported via `event.delta`. Returns the
/// event so call sites can early-exit when nothing happened.
[[nodiscard]] GizmoEvent dragHandle2D(UIContext& ctx, WidgetID id,
                                      Rect bounds) noexcept;

} // namespace threadmaxx::ui
