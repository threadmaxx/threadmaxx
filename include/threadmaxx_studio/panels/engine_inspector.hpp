#pragma once

/// @file panels/engine_inspector.hpp
/// @brief `EntityInspectorPanel` — wraps editor's E2 `Inspector` as
/// an `IStudioPanel`. Renders `listEntities()` as a row table; the
/// row index is the click target.

#include "../panel.hpp"

#include <threadmaxx/Handles.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace threadmaxx::editor {
class Inspector;
class SelectionState;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

class EntityInspectorPanel : public IStudioPanel {
public:
    /// @brief Borrow an Inspector + SelectionState pair. The studio
    /// never owns either — the editor provides them.
    EntityInspectorPanel(editor::Inspector& inspector,
                         editor::SelectionState& selection) noexcept;

    std::string_view id() const noexcept override {
        return "engine.entity_inspector";
    }
    std::string_view title() const noexcept override { return "Entities"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Row count from the most recent `render()` call. Tests
    /// (and hotkey navigation) assert on this rather than counting
    /// drawText ops by hand.
    std::size_t rowCount() const noexcept { return lastHandles_.size(); }

    /// @brief Mouse-click / hotkey-driven row selection. Updates the
    /// borrowed `SelectionState`. Returns `false` if `index` is out
    /// of range for the most recent render.
    bool selectRow(std::size_t index);

private:
    editor::Inspector* inspector_{nullptr};
    editor::SelectionState* selection_{nullptr};
    std::vector<threadmaxx::EntityHandle> lastHandles_;
};

} // namespace threadmaxx::studio
