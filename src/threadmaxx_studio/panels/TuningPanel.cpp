/// @file panels/TuningPanel.cpp

#include <threadmaxx_studio/panels/tuning.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx/Engine.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

const char* modeLabel(threadmaxx::TuningMode m) noexcept {
    switch (m) {
        case threadmaxx::TuningMode::Off:      return "Off";
        case threadmaxx::TuningMode::Active:   return "Active";
        case threadmaxx::TuningMode::Scripted: return "Scripted";
    }
    return "?";
}

} // namespace

void TuningPanel::render(editor::IEditorBackend& backend,
                        IStudioDataSource&) {
    char buf[128];
    const auto mode = engine_->tuningMode();
    const auto* policy = engine_->tuningPolicy();
    const auto* trace  = engine_->tuningTrace();
    std::snprintf(buf, sizeof(buf),
        "mode=%s policy=%s trace=%s entries=%zu",
        modeLabel(mode),
        policy ? "installed" : "none",
        trace  ? "installed" : "none",
        trace  ? trace->size() : 0u);
    backend.drawText(buf, 0.0f, 0.0f);
}

bool TuningPanel::applyPatch(std::uint64_t tick,
                             const threadmaxx::TuningPatch& patch) {
    auto* trace = engine_->tuningTrace();
    if (trace == nullptr) {
        return false;
    }
    trace->record(tick, patch);
    return true;
}

std::size_t TuningPanel::traceSize() const noexcept {
    const auto* trace = engine_->tuningTrace();
    return trace ? trace->size() : 0u;
}

} // namespace threadmaxx::studio
