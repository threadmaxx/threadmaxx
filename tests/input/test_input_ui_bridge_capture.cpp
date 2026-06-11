/// @file test_input_ui_bridge_capture.cpp
/// @brief Capture handshake — host wires UI's wantsMouseCapture /
/// wantsKeyboardCapture back into the input context. This test simulates
/// the round-trip without spinning up a full UI context: we toggle
/// inputCtx.setCaptureMouse() and confirm inputCtx.wantsMouse() reports
/// the new state.

#include "Check.hpp"

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/ui_bridge.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    InputContext ctx;
    ctx.setBackend(&backend);

    // Initially the input library reports no UI capture.
    CHECK(!ctx.wantsMouse());
    CHECK(!ctx.wantsKeyboard());

    // Simulate the UI saying "I want the mouse now".
    ctx.setCaptureMouse(true);

    // Bridge output reflects the same raw mouse state — capture is a
    // signal the host reads off the input context, not a field on UIInput.
    backend.push(MouseButtonEvent{MouseButton::Left, true, 5.0f, 6.0f});
    ctx.beginFrame(1.0f / 60.0f);
    const auto ui = toUIInput(ctx);
    CHECK((ui.mouseButtons & threadmaxx::ui::MouseButton::Left) != 0);
    CHECK(ctx.wantsMouse());
    CHECK(!ctx.wantsKeyboard());
    ctx.endFrame();

    // UI lets go. Host clears the flag; raw events keep flowing.
    ctx.setCaptureMouse(false);
    backend.push(MouseButtonEvent{MouseButton::Left, false, 5.0f, 6.0f});
    ctx.beginFrame(1.0f / 60.0f);
    {
        const auto ui2 = toUIInput(ctx);
        CHECK_EQ(ui2.mouseButtons & threadmaxx::ui::MouseButton::Left, std::uint8_t{0});
    }
    CHECK(!ctx.wantsMouse());
    ctx.endFrame();

    // Keyboard handshake mirrors.
    ctx.setCaptureKeyboard(true);
    CHECK(ctx.wantsKeyboard());

    EXIT_WITH_RESULT();
}
