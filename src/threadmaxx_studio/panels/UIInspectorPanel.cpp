/// @file panels/UIInspectorPanel.cpp
/// @brief ST19 — `UIInspectorPanel` implementation.

#include <threadmaxx_studio/panels/ui_inspector.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_ui/context.hpp>
#include <threadmaxx_ui/draw.hpp>

#include <cstdio>

namespace threadmaxx::studio {

UIInspectorPanel::UIInspectorPanel(const ui::UIContext& context) noexcept
    : context_(&context) {}

void UIInspectorPanel::render(editor::IEditorBackend& backend,
                              IStudioDataSource&) {
    if (context_ == nullptr) {
        backend.drawText("UI Inspector  (available in v1.x — bind a UIContext)",
                         0.0f, 0.0f);
        lastDrawCount_ = 0;
        lastWasStub_   = true;
        return;
    }

    const auto& dl = context_->drawList();
    const auto& cmds = dl.commands();

    std::size_t rects = 0, lines = 0, texts = 0, images = 0, clips = 0;
    for (const auto& c : cmds) {
        switch (c.kind) {
            case ui::DrawCmdKind::Rect:     ++rects;  break;
            case ui::DrawCmdKind::Line:     ++lines;  break;
            case ui::DrawCmdKind::Text:     ++texts;  break;
            case ui::DrawCmdKind::Image:    ++images; break;
            case ui::DrawCmdKind::ClipPush:
            case ui::DrawCmdKind::ClipPop:  ++clips;  break;
        }
    }

    char buf[160];
    std::snprintf(buf, sizeof(buf), "UI Inspector  total=%zu", cmds.size());
    backend.drawText(buf, 0.0f, 0.0f);

    std::snprintf(buf, sizeof(buf),
                  "rects=%zu  lines=%zu  texts=%zu  images=%zu  clips=%zu",
                  rects, lines, texts, images, clips);
    backend.drawText(buf, 0.0f, 16.0f);

    lastDrawCount_ = cmds.size();
    lastWasStub_   = false;
}

} // namespace threadmaxx::studio
