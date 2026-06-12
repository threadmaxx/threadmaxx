/// @file panels/GizmoPanel.cpp

#include <threadmaxx_studio/panels/gizmo.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/selection.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/World.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

void drawAxis(editor::IEditorBackend& backend,
              const editor::AxisHandle& h,
              const char* label) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s axis @ (%.1f,%.1f,%.1f)",
                  label, static_cast<double>(h.origin.x),
                  static_cast<double>(h.origin.y),
                  static_cast<double>(h.origin.z));
    backend.drawText(buf, 0.0f, 0.0f);
}

} // namespace

GizmoPanel::GizmoPanel(threadmaxx::Engine& engine,
                      editor::SelectionState& selection,
                      editor::CommandStack& stack) noexcept
    : engine_(&engine), selection_(&selection), stack_(&stack) {}

void GizmoPanel::render(editor::IEditorBackend& backend,
                        IStudioDataSource&) {
    const auto sel = selection_->currentSelection();
    if (sel.kind != editor::SelectionKind::Entity) {
        backend.drawText("no entity selected", 0.0f, 0.0f);
        return;
    }
    const auto* t = engine_->world().tryGetTransform(sel.entity);
    if (t == nullptr) {
        backend.drawText("(no Transform)", 0.0f, 0.0f);
        return;
    }
    const auto frame = gizmo_.frameFor(t->position);
    drawAxis(backend, frame.x, "X");
    drawAxis(backend, frame.y, "Y");
    drawAxis(backend, frame.z, "Z");
}

bool GizmoPanel::commitTranslate(editor::GizmoAxis axis, float axisDelta) {
    const auto sel = selection_->currentSelection();
    if (sel.kind != editor::SelectionKind::Entity) {
        return false;
    }
    const auto* current = engine_->world().tryGetTransform(sel.entity);
    if (current == nullptr) {
        return false;
    }
    if (!gizmo_.beginDrag(axis)) {
        return false;
    }
    const auto drag = gizmo_.updateDrag(axisDelta);
    gizmo_.endDrag();
    if (!drag.has_value()) {
        return false;
    }
    threadmaxx::Transform oldT = *current;
    threadmaxx::Transform newT = oldT;
    newT.position = newT.position + drag->delta;
    (void)stack_->execute(
        editor::TranslateGizmo::makeTranslateCommand(sel.entity, oldT, newT));
    return true;
}

} // namespace threadmaxx::studio
