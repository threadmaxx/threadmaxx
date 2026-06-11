#pragma once

#include <cstddef>
#include <span>

#include "threadmaxx_input/events.hpp"
#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input {

enum class CursorMode : std::uint8_t {
    Visible = 0,
    Hidden = 1,
    Locked = 2,
};

class IInputBackend {
public:
    virtual ~IInputBackend() = default;

    // Drains queued raw events into `out`. Returns the number written.
    // The backend writes at most `cap` entries and may queue the rest for
    // the next call.
    virtual std::size_t poll(InputEvent* out, std::size_t cap) = 0;

    // Applies a cursor mode change. Default no-op for replay / null sinks.
    virtual void setCursorMode(CursorMode /*mode*/) {}

    // Currently-connected gamepad device ids. Empty span is legal.
    virtual std::span<const DeviceId> connectedGamepads() const = 0;
};

}  // namespace threadmaxx::input
