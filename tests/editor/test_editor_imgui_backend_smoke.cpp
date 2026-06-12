/// @file test_editor_imgui_backend_smoke.cpp
/// @brief E11 — ImGuiBackend exercises drawText / drawRect inside a
/// host-managed ImGui frame; the resulting DrawData is valid. Run
/// only when the ImGui backend is compiled in.

#include "Check.hpp"

#include <threadmaxx_editor/backends/imgui.hpp>

#include <imgui.h>

int main() {
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

    threadmaxx::editor::ImGuiBackend backend{"Editor"};
    CHECK(backend.initialize());
    CHECK(!backend.inFrame());

    backend.beginFrame();
    CHECK(backend.inFrame());
    backend.drawText("hello", 10.0f, 20.0f);
    backend.drawRect(0.0f, 0.0f, 100.0f, 100.0f);
    backend.endFrame();
    CHECK(!backend.inFrame());

    ImGui::Render();
    auto* drawData = ImGui::GetDrawData();
    CHECK(drawData != nullptr);
    CHECK(drawData->Valid);

    backend.shutdown();
    ImGui::DestroyContext();
    EXIT_WITH_RESULT();
}
