#pragma once

/// @file ui_bridge.hpp
/// @brief Lowers an `InputContext`'s per-frame state into the UI library's
/// `UIInput` POD. The only point in `threadmaxx_input` that depends on
/// `threadmaxx::ui` — gated by the `THREADMAXX_INPUT_HAS_UI_BRIDGE` build
/// flag (set by the input library's CMakeLists when both libs are
/// available).
///
/// Direction is one-way: input → UI. The UI library never reads input
/// state; the capture-sink handshake runs through `setCaptureMouse` /
/// `setCaptureKeyboard` on the input side (host wires UI's
/// `wantsMouseCapture()` back in).

#include "threadmaxx_input/context.hpp"
#include "threadmaxx_ui/input.hpp"

namespace threadmaxx::input {

threadmaxx::ui::UIInput toUIInput(const InputContext& ctx) noexcept;

}  // namespace threadmaxx::input
