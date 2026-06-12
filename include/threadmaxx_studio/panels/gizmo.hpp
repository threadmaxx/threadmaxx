#pragma once

/// @file panels/gizmo.hpp
/// @brief `GizmoPanel` — wraps editor's E8 `TranslateGizmo` as an
/// `IStudioPanel`. The gizmo math is pure; this panel handles:
///   * drawing the per-frame `GizmoFrame` via the backend,
///   * routing drag commits through `editor::CommandStack`.

#include "../panel.hpp"

#include <threadmaxx_editor/gizmo.hpp>

#include <string_view>

namespace threadmaxx {
class Engine;
} // namespace threadmaxx

namespace threadmaxx::editor {
class SelectionState;
class CommandStack;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

class GizmoPanel : public IStudioPanel {
public:
    GizmoPanel(threadmaxx::Engine& engine,
               editor::SelectionState& selection,
               editor::CommandStack& stack) noexcept;

    std::string_view id() const noexcept override { return "engine.gizmo"; }
    std::string_view title() const noexcept override { return "Gizmo"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Programmatic drag commit. Reads the current selection's
    /// transform, computes `oldT → newT` by applying `axisDelta` along
    /// `axis`, queues a `SetTransform`-style command. Returns `false`
    /// when no entity is selected or the entity has no Transform.
    bool commitTranslate(editor::GizmoAxis axis, float axisDelta);

    /// @brief Borrowed gizmo math. Tests / advanced hosts may drive
    /// `beginDrag` / `updateDrag` / `endDrag` directly.
    editor::TranslateGizmo&       gizmo()       noexcept { return gizmo_; }
    const editor::TranslateGizmo& gizmo() const noexcept { return gizmo_; }

private:
    threadmaxx::Engine* engine_{nullptr};
    editor::SelectionState* selection_{nullptr};
    editor::CommandStack* stack_{nullptr};
    editor::TranslateGizmo gizmo_;
};

} // namespace threadmaxx::studio
