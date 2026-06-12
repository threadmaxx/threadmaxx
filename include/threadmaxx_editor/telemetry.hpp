#pragma once

/// @file telemetry.hpp
/// @brief Editor telemetry overlay — FPS / frame time / per-system
/// stats, rendered through `IEditorBackend`.

#include "backend.hpp"

#include <threadmaxx/Engine.hpp>

namespace threadmaxx::editor {

/// @brief Which lines the overlay emits. Default: FPS + frame time +
/// per-system stats.
struct OverlayConfig {
    bool showFPS = true;
    bool showFrameTime = true;
    bool showSystemStats = true;
    bool showTraceEvents = false;
    bool showSelectionBounds = true;

    /// @brief Overlay's top-left anchor in editor coordinates.
    float anchorX = 8.0f;
    float anchorY = 8.0f;

    /// @brief Vertical step between lines.
    float lineHeight = 16.0f;
};

/// @brief Polls `Engine::frameSnapshot()` and emits a fixed-shape set of
/// draw calls on the supplied backend. Stateless across ticks — pulls
/// fresh stats every `render()`.
class TelemetryOverlay {
public:
    explicit TelemetryOverlay(const threadmaxx::Engine& engine,
                              OverlayConfig cfg = {}) noexcept;

    void setConfig(const OverlayConfig& cfg) noexcept { config_ = cfg; }
    const OverlayConfig& config() const noexcept { return config_; }

    /// @brief Brackets `backend.beginFrame()` / `backend.endFrame()`
    /// and emits one `drawText` per enabled line. Backend must be
    /// initialized.
    void render(IEditorBackend& backend) const;

private:
    const threadmaxx::Engine* engine_;
    OverlayConfig config_;
};

} // namespace threadmaxx::editor
