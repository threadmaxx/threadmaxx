#pragma once

#include <cstddef>
#include <vector>

#include "threadmaxx_input/backend.hpp"
#include "threadmaxx_input/state.hpp"

namespace threadmaxx::input {

// Per-frame state owner. One per editor pane / game viewport. No globals.
//
// Lifecycle:
//   ctx.setBackend(&backend);
//   for each tick:
//       ctx.beginFrame(dt);
//       // query ctx.state() / ctx.isHeld() / ...
//       ctx.endFrame();
class InputContext {
public:
    InputContext();
    ~InputContext();

    InputContext(const InputContext&) = delete;
    InputContext& operator=(const InputContext&) = delete;
    InputContext(InputContext&&) = delete;
    InputContext& operator=(InputContext&&) = delete;

    void setBackend(IInputBackend* backend) noexcept;
    IInputBackend* backend() const noexcept { return backend_; }

    // Frame loop.
    void beginFrame(float deltaTimeSeconds);
    void endFrame();
    bool frameOpen() const noexcept { return frameOpen_; }

    // Direct queries (reflect state as of the most recent beginFrame).
    const InputState& state() const noexcept { return state_; }

    bool isHeld(Key k) const noexcept { return state_.keys.test(k); }
    bool wasPressed(Key k) const noexcept { return state_.keysPressed.test(k); }
    bool wasReleased(Key k) const noexcept { return state_.keysReleased.test(k); }

    bool isHeld(MouseButton b) const noexcept;
    bool wasPressed(MouseButton b) const noexcept;
    bool wasReleased(MouseButton b) const noexcept;

    bool isHeld(DeviceId pad, GamepadButton b) const noexcept;
    float axis(DeviceId pad, GamepadAxis a) const noexcept;

    // Capture sinks (the UI library sets these; queries inform the host).
    void setCaptureMouse(bool capture) noexcept { captureMouse_ = capture; }
    void setCaptureKeyboard(bool capture) noexcept { captureKeyboard_ = capture; }
    bool wantsMouse() const noexcept { return captureMouse_; }
    bool wantsKeyboard() const noexcept { return captureKeyboard_; }

    // Time accumulator since context construction. Driven by `beginFrame`.
    double simulationTime() const noexcept { return simulationTime_; }
    float lastDeltaTime() const noexcept { return lastDeltaTime_; }
    std::uint64_t frameIndex() const noexcept { return frameIndex_; }

    // Pre-allocates the event drain buffer to `n` entries. Bypasses the
    // first-frame allocation in the no-alloc gate. Idempotent.
    void reserveEvents(std::size_t n);

    // Number of events drained from the backend on the most recent
    // beginFrame call (diagnostic — counts only those that survived the
    // capacity gate, not ones the backend itself dropped).
    std::size_t lastFrameEventCount() const noexcept { return lastFrameEventCount_; }

private:
    void drainBackendEvents();
    void applyEvent(const InputEvent& e);
    void deriveEdges(const InputState& previous);

    IInputBackend* backend_{nullptr};

    InputState state_{};
    InputState previousState_{};

    std::vector<InputEvent> eventBuffer_;
    std::size_t lastFrameEventCount_{0};

    bool frameOpen_{false};
    bool captureMouse_{false};
    bool captureKeyboard_{false};

    double simulationTime_{0.0};
    float lastDeltaTime_{0.0f};
    std::uint64_t frameIndex_{0};
};

}  // namespace threadmaxx::input
