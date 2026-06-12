#pragma once

/// @file panels/property_editor.hpp
/// @brief `PropertyEditorPanel` — wraps editor's E7 `PropertyEditor`
/// + reflect `TypeRegistry`. Edits emit `editor::IEditCommand`
/// instances through the shared `editor::CommandStack`; the studio
/// never bypasses that funnel.

#include "../panel.hpp"

#include <string_view>

#include <threadmaxx_editor/types.hpp>

namespace threadmaxx::editor {
class PropertyEditor;
class SelectionState;
class CommandStack;
} // namespace threadmaxx::editor

namespace threadmaxx::reflect {
class Value;
} // namespace threadmaxx::reflect

namespace threadmaxx::studio {

class PropertyEditorPanel : public IStudioPanel {
public:
    /// @brief Borrow the shared editor surfaces. None are owned.
    PropertyEditorPanel(editor::PropertyEditor& editor,
                        editor::SelectionState& selection,
                        editor::CommandStack& stack) noexcept;

    std::string_view id() const noexcept override {
        return "engine.property_editor";
    }
    std::string_view title() const noexcept override { return "Properties"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Programmatic / textbox-commit edit. Looks up the
    /// current selection, delegates to `PropertyEditor::setField`,
    /// returns its `EditResult`. Returns the editor's `Rejected` /
    /// `Failed` result when no entity is selected.
    editor::EditResult editField(std::string_view typeName,
                                 std::string_view fieldName,
                                 const reflect::Value& newValue);

private:
    editor::PropertyEditor* editor_{nullptr};
    editor::SelectionState* selection_{nullptr};
    editor::CommandStack* stack_{nullptr};
};

} // namespace threadmaxx::studio
