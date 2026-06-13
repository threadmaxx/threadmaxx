/// @file panels/InputPanel.cpp
/// @brief ST17 — `InputPanel` implementation.

#include <threadmaxx_studio/panels/input.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_input/backends/NullBackend.hpp>
#include <threadmaxx_input/context.hpp>
#include <threadmaxx_input/state.hpp>
#include <threadmaxx_input/trace.hpp>

#include <bit>
#include <cstdio>

namespace threadmaxx::studio {

namespace {

std::uint32_t countKeysHeld(const input::detail::KeyBitset& bits) noexcept {
    std::uint32_t n = 0;
    for (const auto& w : bits.words) n += static_cast<std::uint32_t>(std::popcount(w));
    return n;
}

std::uint32_t connectedGamepads(const input::InputState& s) noexcept {
    std::uint32_t n = 0;
    for (const auto& g : s.gamepads) if (g.connected) ++n;
    return n;
}

} // namespace

InputPanel::InputPanel(const input::InputContext& context) noexcept
    : context_(&context) {}

bool InputPanel::recordCurrentFrame() {
    if (context_ == nullptr || trace_ == nullptr) return false;
    trace_->recordCurrentFrame(*context_);
    return true;
}

bool InputPanel::replayFrame(std::uint64_t frameIndex) {
    if (trace_ == nullptr || replayBackend_ == nullptr) return false;
    return trace_->replayTo(*replayBackend_, frameIndex);
}

void InputPanel::render(editor::IEditorBackend& backend,
                        IStudioDataSource&) {
    if (context_ == nullptr) {
        backend.drawText("Input: <detached>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }

    const auto& state = context_->state();
    const auto held = countKeysHeld(state.keys);
    const auto pads = connectedGamepads(state);

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Input  mods=0x%02X  keys=%u  pads=%u  chars=%u",
                  state.modifiers, held, pads,
                  static_cast<unsigned>(state.charCount));
    backend.drawText(buf, 0.0f, 0.0f);

    std::snprintf(buf, sizeof(buf),
                  "Mouse  x=%.1f y=%.1f  dx=%.1f dy=%.1f  buttons=0x%02X",
                  static_cast<double>(state.mouse.x),
                  static_cast<double>(state.mouse.y),
                  static_cast<double>(state.mouse.dx),
                  static_cast<double>(state.mouse.dy),
                  state.mouse.buttons);
    backend.drawText(buf, 0.0f, 16.0f);

    std::size_t rows = 2;
    if (trace_ != nullptr) {
        std::snprintf(buf, sizeof(buf),
                      "Trace  frames=%llu  replayReady=%s",
                      static_cast<unsigned long long>(trace_->frameCount()),
                      replayBackend_ != nullptr ? "yes" : "no");
        backend.drawText(buf, 0.0f, 32.0f);
        ++rows;
    }
    lastRows_ = rows;
}

} // namespace threadmaxx::studio
