/// @file panels/PropertyEditorPanel.cpp

#include <threadmaxx_studio/panels/property_editor.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/properties.hpp>
#include <threadmaxx_editor/selection.hpp>

namespace threadmaxx::studio {

PropertyEditorPanel::PropertyEditorPanel(editor::PropertyEditor& ed,
                                         editor::SelectionState& selection,
                                         editor::CommandStack& stack) noexcept
    : editor_(&ed), selection_(&selection), stack_(&stack) {}

void PropertyEditorPanel::render(editor::IEditorBackend& backend,
                                 IStudioDataSource&) {
    const auto sel = selection_->currentSelection();
    if (sel.kind != editor::SelectionKind::Entity) {
        backend.drawText("no entity selected", 0.0f, 0.0f);
        return;
    }
    const auto types = editor_->componentsOn(sel.entity);
    float y = 0.0f;
    for (const auto& t : types) {
        backend.drawText(t, 0.0f, y);
        y += 16.0f;
    }
    if (types.empty()) {
        backend.drawText("(no components)", 0.0f, 0.0f);
    }
}

editor::EditResult PropertyEditorPanel::editField(
    std::string_view typeName, std::string_view fieldName,
    const reflect::Value& newValue) {
    const auto sel = selection_->currentSelection();
    if (sel.kind != editor::SelectionKind::Entity) {
        return editor::EditResult::Rejected;
    }
    return editor_->setField(*stack_, sel.entity, typeName, fieldName,
                             newValue);
}

} // namespace threadmaxx::studio
