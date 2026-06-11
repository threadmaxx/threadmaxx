/// @file test_input_trace_record_replay.cpp
/// @brief Record 100 frames of synthetic events; replay through a fresh
/// context. Per-frame InputState snapshots must be byte-identical between
/// the original and the replay.

#include "Check.hpp"

#include <cstring>

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/trace.hpp"

namespace {

bool stateEquals(const threadmaxx::input::InputState& a,
                 const threadmaxx::input::InputState& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

}  // namespace

int main() {
    using namespace threadmaxx::input;

    // Original run.
    NullBackend backendA;
    InputContext ctxA;
    ctxA.setBackend(&backendA);

    InputTrace trace;
    trace.reserve(100);

    std::vector<InputState> snapshotsA;
    snapshotsA.reserve(100);

    auto driveOriginal = [&](int frame) {
        // A varied stream — covers every variant alternative.
        const int phase = frame % 8;
        switch (phase) {
            case 0: backendA.push(KeyEvent{Key::A, Modifiers::None, true}); break;
            case 1: backendA.push(KeyEvent{Key::A, Modifiers::None, false}); break;
            case 2: backendA.push(MouseButtonEvent{MouseButton::Left, true, 5.0f, 6.0f}); break;
            case 3: backendA.push(MouseButtonEvent{MouseButton::Left, false, 5.0f, 6.0f}); break;
            case 4: backendA.push(MouseMoveEvent{static_cast<float>(frame), 0.0f, 1.0f, 0.0f}); break;
            case 5: backendA.push(MouseWheelEvent{0.0f, 1.0f}); break;
            case 6: backendA.push(GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.5f}); break;
            case 7: backendA.push(CharEvent{'a'}); break;
        }
    };

    for (int i = 0; i < 100; ++i) {
        driveOriginal(i);
        ctxA.beginFrame(1.0f / 60.0f);
        trace.recordCurrentFrame(ctxA);
        snapshotsA.push_back(ctxA.state());
        ctxA.endFrame();
    }

    CHECK_EQ(trace.frameCount(), std::uint64_t{100});

    // Replay through a fresh backend + context. No additional backend
    // events — the trace is the sole source.
    NullBackend backendB;
    InputContext ctxB;
    ctxB.setBackend(&backendB);

    for (std::uint64_t i = 0; i < trace.frameCount(); ++i) {
        CHECK(trace.replayTo(backendB, i));
        ctxB.beginFrame(1.0f / 60.0f);
        CHECK(stateEquals(ctxB.state(), snapshotsA[static_cast<std::size_t>(i)]));
        ctxB.endFrame();
    }

    EXIT_WITH_RESULT();
}
