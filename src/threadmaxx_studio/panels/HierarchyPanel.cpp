/// @file panels/HierarchyPanel.cpp
/// @brief ST7 — `HierarchyPanel` implementation.

#include <threadmaxx_studio/panels/hierarchy.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/hierarchy.hpp>
#include <threadmaxx_editor/selection.hpp>

#include <string>

namespace threadmaxx::studio {

HierarchyPanel::HierarchyPanel(editor::HierarchyView& view,
                               editor::SelectionState& selection) noexcept
    : view_(&view), selection_(&selection) {}

void HierarchyPanel::render(editor::IEditorBackend& backend,
                            IStudioDataSource&) {
    const auto rows = view_->tree();
    lastRows_.clear();
    lastRows_.reserve(rows.size());

    float y = 0.0f;
    for (const auto& r : rows) {
        lastRows_.push_back(RowMeta{r.handle, r.depth, r.hasChildren,
                                    r.expanded});

        std::string line;
        line.reserve(r.label.size() + 4);
        if (r.hasChildren) {
            line += (r.expanded ? "- " : "+ ");
        } else {
            line += "  ";
        }
        line += r.label;
        backend.drawText(line, static_cast<float>(r.depth) * kIndentPx, y);
        y += 16.0f;
    }
}

bool HierarchyPanel::selectRow(std::size_t index) {
    if (index >= lastRows_.size()) return false;
    selection_->select(lastRows_[index].handle);
    return true;
}

bool HierarchyPanel::toggleRow(std::size_t index) {
    if (index >= lastRows_.size()) return false;
    const auto& row = lastRows_[index];
    view_->setExpanded(row.handle, !row.expanded);
    return true;
}

} // namespace threadmaxx::studio
