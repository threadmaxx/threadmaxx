#pragma once

#include <cstddef>
#include <vector>

#include "threadmaxx_input/backend.hpp"
#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input {

// Default test sink. Records cursor mode changes and exposes a queue that
// host tests can populate to drive the context. Also serves as the
// substrate for InputTrace::replay (I8).
class NullBackend final : public IInputBackend {
public:
    NullBackend() = default;

    // Append a synthetic event for the next poll() call.
    void push(const InputEvent& e);
    void pushAll(const InputEvent* begin, std::size_t count);

    std::size_t poll(InputEvent* out, std::size_t cap) override;

    void setCursorMode(CursorMode mode) override;
    CursorMode cursorMode() const noexcept { return cursorMode_; }
    std::size_t cursorModeChangeCount() const noexcept { return cursorChangeCount_; }

    void setConnectedGamepads(std::vector<DeviceId> ids);
    std::span<const DeviceId> connectedGamepads() const override;

    // Reserve underlying queue capacity for zero-alloc steady state.
    void reserve(std::size_t n);
    std::size_t pendingCount() const noexcept { return queue_.size() - head_; }
    void clear() noexcept;

private:
    std::vector<InputEvent> queue_;
    std::size_t head_{0};
    std::vector<DeviceId> connectedGamepads_;
    CursorMode cursorMode_{CursorMode::Visible};
    std::size_t cursorChangeCount_{0};
};

}  // namespace threadmaxx::input
