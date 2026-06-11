/// @file test_input_cursor_mode_default.cpp
/// @brief Default cursor mode is Visible; setCursorMode(...) forwards to
/// the backend exactly once per distinct mode change.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/cursor.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    CHECK_EQ(static_cast<int>(ctx.cursorMode()), static_cast<int>(CursorMode::Visible));
    CHECK_EQ(static_cast<int>(backend.cursorMode()), static_cast<int>(CursorMode::Visible));
    CHECK_EQ(backend.cursorModeChangeCount(), std::size_t{0});

    setCursorMode(ctx, CursorMode::Locked);
    CHECK_EQ(static_cast<int>(ctx.cursorMode()), static_cast<int>(CursorMode::Locked));
    CHECK_EQ(static_cast<int>(backend.cursorMode()), static_cast<int>(CursorMode::Locked));
    CHECK_EQ(backend.cursorModeChangeCount(), std::size_t{1});

    // Redundant set to the same mode should NOT bump the change counter.
    setCursorMode(ctx, CursorMode::Locked);
    CHECK_EQ(backend.cursorModeChangeCount(), std::size_t{1});

    setCursorMode(ctx, CursorMode::Hidden);
    CHECK_EQ(static_cast<int>(backend.cursorMode()), static_cast<int>(CursorMode::Hidden));
    CHECK_EQ(backend.cursorModeChangeCount(), std::size_t{2});

    // Null-backend safety: setCursorMode against no backend just stores
    // the value locally.
    ctx.setBackend(nullptr);
    setCursorMode(ctx, CursorMode::Visible);
    CHECK_EQ(static_cast<int>(ctx.cursorMode()), static_cast<int>(CursorMode::Visible));

    EXIT_WITH_RESULT();
}
