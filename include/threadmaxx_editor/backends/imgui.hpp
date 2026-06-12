#pragma once

/// @file backends/imgui.hpp
/// @brief Dear ImGui-backed IEditorBackend.
///
/// E11 — first concrete UI backend. Available when the editor was
/// built with `THREADMAXX_EDITOR_HAS_IMGUI_BACKEND=1`
/// (auto-enabled by `-DTHREADMAXX_EDITOR_FETCH_IMGUI=ON`).
///
/// The backend translates `drawText` / `drawRect` into ImGui calls
/// inside a host-owned ImGui frame. The HOST is responsible for the
/// per-tick `ImGui::NewFrame()` / `ImGui::Render()` brackets and for
/// driving the actual ImGui renderer (Vulkan, OpenGL, etc.).
///
/// Typical host loop:
///
///   ImGui::NewFrame();
///   editor.overlay().render(backend);   // one ImGui window
///   editor.inspector().render(backend); // another window
///   ImGui::Render();
///   yourRenderer->renderDrawData(ImGui::GetDrawData());
///
/// `ImGuiBackend::beginFrame()` opens an `ImGui::Begin(...)` window;
/// `endFrame()` closes it. Each editor panel that calls
/// `overlay.render(backend)` thus produces one ImGui window.

#include "../backend.hpp"

#include <string>

namespace threadmaxx::editor {

class ImGuiBackend final : public IEditorBackend {
public:
    /// @param windowTitle Default ImGui window title used by
    /// `beginFrame()`. Hosts may set per-panel titles via
    /// `setWindowTitle` before each panel's `render` call.
    explicit ImGuiBackend(std::string windowTitle = "Editor") noexcept;

    /// @brief Returns true iff an ImGui context is currently bound.
    /// The host must call `ImGui::CreateContext()` (and configure
    /// fonts + display size) before calling `setBackend(this)`.
    bool initialize() override;

    void shutdown() override;

    /// @brief `ImGui::Begin(windowTitle)`.
    void beginFrame() override;

    /// @brief `ImGui::End()`.
    void endFrame() override;

    void drawText(std::string_view text, float x, float y) override;
    void drawRect(float x, float y, float w, float h) override;

    /// @brief Set the window title used by the next `beginFrame()`.
    void setWindowTitle(std::string title) noexcept {
        windowTitle_ = std::move(title);
    }

    const std::string& windowTitle() const noexcept { return windowTitle_; }
    bool inFrame() const noexcept { return inFrame_; }

private:
    std::string windowTitle_;
    bool inFrame_{false};
};

} // namespace threadmaxx::editor
