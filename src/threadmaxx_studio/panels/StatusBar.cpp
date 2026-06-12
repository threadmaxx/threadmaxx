/// @file panels/StatusBar.cpp

#include <threadmaxx_studio/panels/status_bar.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

std::string formatStatus(const EngineFrameSummary& s) {
    if (s.paused) {
        return "PAUSED";
    }
    char buf[64];
    int fps = 0;
    if (s.lastStepSeconds > 0.0) {
        const double frac = 1.0 / s.lastStepSeconds + 0.5;
        if (frac > 0.0 && frac < 1.0e9) {
            fps = static_cast<int>(frac);
        }
    }
    std::snprintf(buf, sizeof(buf), "FPS %d", fps);
    return std::string(buf);
}

} // namespace

void StatusBar::render(editor::IEditorBackend& backend,
                       IStudioDataSource& source) {
    const auto snap = source.engineSnapshot();
    if (snap.has_value()) {
        lastStatus_ = formatStatus(*snap);
    } else {
        lastStatus_ = "no engine";
    }
    backend.drawText(lastStatus_, 0.0f, 0.0f);
}

} // namespace threadmaxx::studio
