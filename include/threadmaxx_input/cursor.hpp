#pragma once

// Cursor-mode convenience facade. `CursorMode` itself is declared in
// `backend.hpp` next to `IInputBackend::setCursorMode`; this header just
// re-exports it and provides free-function shortcuts so host code can
// say `setCursorMode(ctx, CursorMode::Locked)` without reaching through
// the context API.

#include "threadmaxx_input/backend.hpp"
#include "threadmaxx_input/context.hpp"

namespace threadmaxx::input {

inline void setCursorMode(InputContext& ctx, CursorMode mode) noexcept {
    ctx.setCursorMode(mode);
}

inline CursorMode cursorMode(const InputContext& ctx) noexcept {
    return ctx.cursorMode();
}

}  // namespace threadmaxx::input
