#pragma once

/// @file panels/hierarchy.hpp
/// @brief ST7 — `HierarchyPanel` wraps editor E12's `HierarchyView`
/// as an `IStudioPanel`. Renders one row per visible node, indented
/// by depth, with a leading `+` / `-` chevron reflecting the
/// expansion state. The studio shell rebinds row clicks to
/// `selectRow` for selection and `toggleRow` for expand / collapse.

#include "../panel.hpp"

#include <threadmaxx/Handles.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace threadmaxx::editor {
class HierarchyView;
class SelectionState;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

class HierarchyPanel : public IStudioPanel {
public:
    /// @brief Borrow a HierarchyView + SelectionState pair. The
    /// studio never owns either — the editor provides them.
    HierarchyPanel(editor::HierarchyView& view,
                   editor::SelectionState& selection) noexcept;

    std::string_view id() const noexcept override {
        return "engine.hierarchy";
    }
    std::string_view title() const noexcept override { return "Hierarchy"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Row count from the most recent `render()`.
    [[nodiscard]] std::size_t rowCount() const noexcept {
        return lastRows_.size();
    }

    /// @brief Update the borrowed selection to row @p index. Returns
    /// false if out of range for the most recent render.
    bool selectRow(std::size_t index);

    /// @brief Flip the expansion state of the node at row @p index in
    /// the view. Returns false if out of range. Does NOT affect
    /// selection.
    bool toggleRow(std::size_t index);

    /// @brief Pixel step used to indent each depth level. Tests assert
    /// on this; the studio shell may resize it but not change the
    /// indentation model.
    static constexpr float kIndentPx = 12.0f;

private:
    struct RowMeta {
        threadmaxx::EntityHandle handle;
        std::uint32_t            depth;
        bool                     hasChildren;
        bool                     expanded;
    };
    editor::HierarchyView*   view_{nullptr};
    editor::SelectionState*  selection_{nullptr};
    std::vector<RowMeta>     lastRows_;
};

} // namespace threadmaxx::studio
