/// @file TelemetryOverlay.cpp
/// @brief Polls Engine::frameSnapshot() and emits an overlay through
/// IEditorBackend.

#include "threadmaxx_editor/telemetry.hpp"

#include <threadmaxx/Stats.hpp>
#include <threadmaxx/Trace.hpp>

#include <cstdio>

namespace threadmaxx::editor {

namespace {

void formatLine(char* out, std::size_t cap, const char* fmt, double v) {
    std::snprintf(out, cap, fmt, v);
}

} // namespace

TelemetryOverlay::TelemetryOverlay(const threadmaxx::Engine& engine,
                                   OverlayConfig cfg) noexcept
    : engine_(&engine), config_(cfg) {}

void TelemetryOverlay::render(IEditorBackend& backend) const {
    const auto snap = engine_->frameSnapshot();
    const auto& stats = snap.engine;

    backend.beginFrame();
    float y = config_.anchorY;
    char buf[96];

    if (config_.showFPS) {
        const double fps = stats.lastStepSeconds > 0.0
                               ? 1.0 / stats.lastStepSeconds
                               : 0.0;
        formatLine(buf, sizeof(buf), "FPS: %.1f", fps);
        backend.drawText(buf, config_.anchorX, y);
        y += config_.lineHeight;
    }

    if (config_.showFrameTime) {
        formatLine(buf, sizeof(buf), "Frame: %.2f ms",
                   stats.lastStepSeconds * 1000.0);
        backend.drawText(buf, config_.anchorX, y);
        y += config_.lineHeight;
    }

    if (config_.showSystemStats) {
        for (const auto& sys : snap.systems) {
            const char* name = sys.name ? sys.name : "<unnamed>";
            std::snprintf(buf, sizeof(buf), "%s: %.2f ms",
                          name, sys.lastUpdateSeconds * 1000.0);
            backend.drawText(buf, config_.anchorX, y);
            y += config_.lineHeight;
        }
    }

    backend.endFrame();
}

} // namespace threadmaxx::editor
