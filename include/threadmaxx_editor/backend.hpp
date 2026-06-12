#pragma once

/// @file backend.hpp
/// @brief Editor UI backend contract.
///
/// The editor core stays UI-toolkit-agnostic. Every panel and overlay
/// emits its UI through this interface. `HeadlessBackend` records every
/// call into a captured frame for tests; `ImGuiBackend` (E11) binds to
/// Dear ImGui; future backends can target remote / web / native.

#include <string_view>

namespace threadmaxx::editor {

/// @brief Renderer-neutral backend for editor draw calls.
///
/// Lifecycle: `initialize()` once, then per-frame `beginFrame()` /
/// `endFrame()` bracketing any number of `drawText` / `drawRect` calls.
/// `shutdown()` once when the session tears down.
///
/// @thread_safety Single-threaded by convention; the editor never calls
/// into a backend from worker jobs.
class IEditorBackend {
public:
    virtual ~IEditorBackend() = default;

    /// @brief Bind GPU / OS resources. Returns true on success; the
    /// session refuses to attach if this returns false.
    virtual bool initialize() = 0;

    /// @brief Release GPU / OS resources. Called once per session.
    virtual void shutdown() = 0;

    /// @brief Open a frame. Must precede any draw call.
    virtual void beginFrame() = 0;

    /// @brief Close a frame and (for real backends) submit it.
    virtual void endFrame() = 0;

    /// @brief Draw a text string at logical (x, y) in editor coordinates.
    /// The backend may copy `text` or hold it for the duration of the
    /// frame; callers should not delete the underlying buffer until
    /// after `endFrame()`.
    virtual void drawText(std::string_view text, float x, float y) = 0;

    /// @brief Draw a filled rectangle in editor coordinates.
    virtual void drawRect(float x, float y, float w, float h) = 0;
};

} // namespace threadmaxx::editor
