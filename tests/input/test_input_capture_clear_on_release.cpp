/// @file test_input_capture_clear_on_release.cpp
/// @brief Capture sinks are sticky — they persist across frames until the
/// host explicitly clears them. endFrame does NOT auto-release.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    ctx.setCaptureMouse(true);
    ctx.setCaptureKeyboard(true);

    // Pump several frames; capture flags must NOT reset.
    for (int i = 0; i < 5; ++i) {
        ctx.beginFrame(1.0f / 60.0f);
        CHECK(ctx.wantsMouse());
        CHECK(ctx.wantsKeyboard());
        ctx.endFrame();
    }

    // Independent release: clearing mouse leaves keyboard alone.
    ctx.setCaptureMouse(false);
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(!ctx.wantsMouse());
    CHECK(ctx.wantsKeyboard());
    ctx.endFrame();

    // And vice versa.
    ctx.setCaptureMouse(true);
    ctx.setCaptureKeyboard(false);
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.wantsMouse());
    CHECK(!ctx.wantsKeyboard());
    ctx.endFrame();

    EXIT_WITH_RESULT();
}
