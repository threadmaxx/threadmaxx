/// @file panels/EntityInspectorPanel.cpp

#include <threadmaxx_studio/panels/engine_inspector.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/inspect.hpp>
#include <threadmaxx_editor/selection.hpp>

namespace threadmaxx::studio {

EntityInspectorPanel::EntityInspectorPanel(
    editor::Inspector& inspector,
    editor::SelectionState& selection) noexcept
    : inspector_(&inspector), selection_(&selection) {}

void EntityInspectorPanel::render(editor::IEditorBackend& backend,
                                  IStudioDataSource&) {
    const auto rows = inspector_->listEntities();
    lastHandles_.clear();
    lastHandles_.reserve(rows.size());
    float y = 0.0f;
    for (const auto& row : rows) {
        lastHandles_.push_back(row.handle);
        backend.drawText(row.label, 0.0f, y);
        y += 16.0f;
    }
}

bool EntityInspectorPanel::selectRow(std::size_t index) {
    if (index >= lastHandles_.size()) {
        return false;
    }
    selection_->select(lastHandles_[index]);
    return true;
}

} // namespace threadmaxx::studio
