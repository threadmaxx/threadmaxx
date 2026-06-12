/// @file panels/ConsolePanel.cpp

#include <threadmaxx_studio/panels/console.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/console.hpp>

namespace threadmaxx::studio {

void ConsolePanel::render(editor::IEditorBackend& backend,
                          IStudioDataSource&) {
    // Walk console history newest-first and emit one drawText per
    // line. Future ST batches add an input row + scroll state; this
    // is the v0 panel that proves the wrapping contract.
    const auto hist = console_->history();
    float y = 0.0f;
    for (const auto& line : hist) {
        backend.drawText(line, 0.0f, y);
        y += 12.0f;
    }
}

} // namespace threadmaxx::studio
