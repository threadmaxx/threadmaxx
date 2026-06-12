#pragma once

/// @file gizmo.hpp
/// @brief Transform gizmo math — handle positions, ray hit-tests, and
/// the drag command synthesis.
///
/// The gizmo MATH is testable headlessly: a v1.0 `GizmoFrame` is the
/// data the backend draws each frame. Backends translate the frame
/// into their own draw calls; the math has no UI dependency.

#include "commands.hpp"
#include "types.hpp"

#include <cstdint>
#include <optional>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>

namespace threadmaxx::editor {

enum class GizmoMode : std::uint8_t {
    Translate,
    // Rotate / Scale ship in v1.x.
};

enum class GizmoAxis : std::uint8_t {
    None = 0,
    X,
    Y,
    Z,
};

/// @brief One axis handle for the translate gizmo. World position is
/// `origin + axisDir * length`.
struct AxisHandle {
    GizmoAxis axis{GizmoAxis::None};
    threadmaxx::Vec3 origin{};
    threadmaxx::Vec3 axisDir{};   // unit vector
    float length{1.0f};
    float radius{0.1f};           // hit-test radius
};

/// @brief Snapshot of the gizmo's per-frame state. Backends draw lines
/// `[origin, origin + axisDir*length]` per axis, in axis-coded colors
/// (X=red, Y=green, Z=blue, the convention).
struct GizmoFrame {
    GizmoMode mode{GizmoMode::Translate};
    threadmaxx::Vec3 origin{};
    AxisHandle x{};
    AxisHandle y{};
    AxisHandle z{};
    GizmoAxis activeDrag{GizmoAxis::None};
};

/// @brief 3D ray for hit-testing.
struct Ray3 {
    threadmaxx::Vec3 origin{};
    threadmaxx::Vec3 dir{};       // not required to be normalized
};

/// @brief Result of dragging a handle: a positional delta in world
/// space along the active axis.
struct GizmoDragResult {
    threadmaxx::Vec3 delta{};     // newPos = oldPos + delta
};

/// @brief Pure-math translate gizmo. Stateless except for the active
/// drag axis recorded on `beginDrag` / cleared on `endDrag`.
class TranslateGizmo {
public:
    /// @brief Build the per-frame snapshot for an entity at `entityPos`.
    GizmoFrame frameFor(const threadmaxx::Vec3& entityPos) const noexcept;

    /// @brief Closest-axis ray hit-test. Returns the axis hit (or
    /// `GizmoAxis::None`), parameterized by `frame`'s handles.
    GizmoAxis hitTest(const GizmoFrame& frame,
                      const Ray3& ray) const noexcept;

    /// @brief Begin a drag on `axis`. Returns false on `GizmoAxis::None`.
    bool beginDrag(GizmoAxis axis) noexcept;

    /// @brief Convert a per-axis cursor delta (world units) into a
    /// position delta along the active axis. Returns nullopt when no
    /// drag is active.
    std::optional<GizmoDragResult>
    updateDrag(float axisDelta) const noexcept;

    /// @brief Cancel the current drag. No-op when none is active.
    void endDrag() noexcept;

    GizmoAxis activeAxis() const noexcept { return activeAxis_; }

    /// @brief Build a `SetTransform`-style command that sets the
    /// entity's transform to `newTransform`. Owned by the caller —
    /// pass to `CommandStack::execute`.
    static std::unique_ptr<IEditCommand>
    makeTranslateCommand(threadmaxx::EntityHandle target,
                         const threadmaxx::Transform& oldT,
                         const threadmaxx::Transform& newT);

private:
    GizmoAxis activeAxis_{GizmoAxis::None};
};

} // namespace threadmaxx::editor
