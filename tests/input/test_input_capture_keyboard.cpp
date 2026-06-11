/// @file test_input_capture_keyboard.cpp
/// @brief Same shape as the mouse-capture test but for keyboard.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    CHECK(!ctx.wantsKeyboard());

    ctx.setCaptureKeyboard(true);
    CHECK(ctx.wantsKeyboard());

    backend.push(KeyEvent{Key::A, Modifiers::None, true});
    backend.push(CharEvent{'a'});
    ctx.beginFrame(1.0f / 60.0f);
    CHECK(ctx.isHeld(Key::A));
    CHECK_EQ(ctx.state().charCount, std::uint8_t{1});
    CHECK_EQ(ctx.state().chars[0], std::uint32_t{'a'});
    CHECK(ctx.wantsKeyboard());
    ctx.endFrame();

    // Capture survives the frame; mouse capture is independent.
    CHECK(!ctx.wantsMouse());
    ctx.setCaptureKeyboard(false);
    CHECK(!ctx.wantsKeyboard());

    EXIT_WITH_RESULT();
}
