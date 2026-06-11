#pragma once

/// @file GlfwBackend.hpp
/// @brief Adapter that turns GLFW's raw callbacks into `InputEvent`s.
///
/// This backend does NOT link GLFW. The host owns the window + the
/// callbacks; this class just provides translation helpers that take
/// GLFW's int-coded key / button / action values and queue the right
/// `InputEvent` shape. That keeps `threadmaxx_input` portable while still
/// shipping a documented integration path for the most common GLFW host.
///
/// Usage sketch (from a host that links GLFW):
/// @code
///   GlfwBackend backend;
///   glfwSetKeyCallback(window, [](GLFWwindow* w, int k, int sc, int act, int m){
///       auto* b = static_cast<GlfwBackend*>(glfwGetWindowUserPointer(w));
///       b->pushGlfwKey(k, sc, act, m);
///   });
/// @endcode

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "threadmaxx_input/backend.hpp"
#include "threadmaxx_input/events.hpp"
#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input {

class GlfwBackend final : public IInputBackend {
public:
    // GLFW key / action codes are stable ints. We accept them as ints so
    // the consumer doesn't need to include <GLFW/glfw3.h> in this header.

    // Translate a GLFW key callback (glfwSetKeyCallback) — `action` is 1 for
    // press, 0 for release, 2 for repeat (which we skip — autorepeat is the
    // host's call to translate).
    void pushGlfwKey(int glfwKey, int scancode, int glfwAction, int glfwMods);

    // Translate a GLFW char callback (glfwSetCharCallback).
    void pushGlfwChar(std::uint32_t codepoint);

    // Translate a GLFW cursor-pos callback (glfwSetCursorPosCallback).
    // `dx/dy` is computed from the previous pos.
    void pushGlfwCursorPos(double xpos, double ypos);

    // Translate a GLFW mouse-button callback (glfwSetMouseButtonCallback).
    void pushGlfwMouseButton(int glfwButton, int glfwAction, int glfwMods);

    // Translate a GLFW scroll callback (glfwSetScrollCallback).
    void pushGlfwScroll(double xoffset, double yoffset);

    // Translate a GLFW gamepad button event (poll glfwGetGamepadState).
    void pushGlfwGamepadButton(DeviceId pad, int glfwButton, int glfwAction);
    void pushGlfwGamepadAxis(DeviceId pad, int glfwAxis, float value);

    void pushDeviceConnect(DeviceId pad, bool gamepad);
    void pushDeviceDisconnect(DeviceId pad);

    std::size_t poll(InputEvent* out, std::size_t cap) override;

    void setCursorMode(CursorMode mode) override;
    CursorMode cursorMode() const noexcept { return cursorMode_; }
    std::size_t cursorModeChangeCount() const noexcept { return cursorChangeCount_; }

    void setConnectedGamepads(std::vector<DeviceId> ids);
    std::span<const DeviceId> connectedGamepads() const override;

    void reserve(std::size_t n);
    std::size_t pendingCount() const noexcept { return queue_.size() - head_; }
    void clear() noexcept;

private:
    std::vector<InputEvent> queue_;
    std::size_t head_{0};
    std::vector<DeviceId> connectedGamepads_;
    CursorMode cursorMode_{CursorMode::Visible};
    std::size_t cursorChangeCount_{0};
    double prevCursorX_{0.0};
    double prevCursorY_{0.0};
    bool cursorPosInitialized_{false};
};

}  // namespace threadmaxx::input
