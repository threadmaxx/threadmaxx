/// @file ImGuiBackend.cpp
/// @brief Dear ImGui editor backend (E11).

#include "threadmaxx_editor/backends/imgui.hpp"

#include <imgui.h>

#include <string>
#include <utility>

namespace threadmaxx::editor {

ImGuiBackend::ImGuiBackend(std::string title) noexcept
    : windowTitle_(std::move(title)) {}

bool ImGuiBackend::initialize() {
    return ImGui::GetCurrentContext() != nullptr;
}

void ImGuiBackend::shutdown() {
    inFrame_ = false;
}

void ImGuiBackend::beginFrame() {
    ImGui::Begin(windowTitle_.c_str());
    inFrame_ = true;
}

void ImGuiBackend::endFrame() {
    ImGui::End();
    inFrame_ = false;
}

void ImGuiBackend::drawText(std::string_view text, float x, float y) {
    ImGui::SetCursorPos(ImVec2(x, y));
    // ImGui::TextUnformatted needs a (begin, end) range — no nul required.
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void ImGuiBackend::drawRect(float x, float y, float w, float h) {
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 a(winPos.x + x, winPos.y + y);
    const ImVec2 b(winPos.x + x + w, winPos.y + y + h);
    // 0xFFFFFFFFu == IM_COL32(255,255,255,255); spelled out to avoid
    // the upstream macro's C-style casts under -Wold-style-cast.
    constexpr ImU32 kWhite = 0xFFFFFFFFu;
    dl->AddRect(a, b, kWhite);
}

} // namespace threadmaxx::editor
