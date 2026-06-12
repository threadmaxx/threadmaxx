/// @file test_editor_imgui_overlay.cpp
/// @brief E11 — TelemetryOverlay renders through ImGuiBackend without
/// crashing; the resulting DrawData carries at least one DrawList
/// (the "Editor" window).

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/backends/imgui.hpp>
#include <threadmaxx_editor/telemetry.hpp>

#include <imgui.h>

int main() {
    threadmaxx::editor::test::ScopedEngine env;
    env.engine().step();

    ImGui::CreateContext();
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }

    ImGui::NewFrame();

    threadmaxx::editor::ImGuiBackend backend{"Telemetry"};
    backend.initialize();
    // Pin the window so ImGui produces a draw list even on the very
    // first frame (default position is off-screen).
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
    threadmaxx::editor::TelemetryOverlay overlay{env.engine()};
    overlay.render(backend);

    ImGui::Render();
    auto* dd = ImGui::GetDrawData();
    CHECK(dd != nullptr);
    CHECK(dd->Valid);
    CHECK(dd->CmdListsCount >= 1);

    ImGui::DestroyContext();
    EXIT_WITH_RESULT();
}
