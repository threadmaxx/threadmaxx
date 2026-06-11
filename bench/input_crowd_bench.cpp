// Frame-build throughput for threadmaxx_input. Steady-state target:
// < 50 µs / frame for 200 actions, 800 bindings, 16 events per frame.

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/binding.hpp"
#include "threadmaxx_input/context.hpp"

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    backend.reserve(256);
    InputContext ctx;
    ctx.setBackend(&backend);
    ctx.reserveEvents(256);

    std::vector<ActionId> ids;
    ids.reserve(200);
    for (int i = 0; i < 200; ++i) {
        ids.push_back(actionId(std::string("Action_") + std::to_string(i)));
    }

    BindingSet bs;
    for (int i = 0; i < 200; ++i) {
        const auto idx = static_cast<std::uint16_t>(i % 26);
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::key(static_cast<Key>(static_cast<std::uint16_t>(Key::A) + idx)));
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::mouseButton(static_cast<MouseButton>(i % 5)));
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::gamepadButton(static_cast<GamepadButton>(i % 15)));
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::gamepadAxisPositive(static_cast<GamepadAxis>(i % 6), 0.4f));
    }
    ctx.setBindings(bs);

    InputEvent slate[16] = {
        KeyEvent{Key::A, Modifiers::None, true},
        KeyEvent{Key::B, Modifiers::None, true},
        KeyEvent{Key::A, Modifiers::None, false},
        KeyEvent{Key::B, Modifiers::None, false},
        MouseMoveEvent{10.0f, 20.0f, 1.0f, 0.0f},
        MouseButtonEvent{MouseButton::Left, true, 10.0f, 20.0f},
        MouseButtonEvent{MouseButton::Left, false, 10.0f, 20.0f},
        MouseWheelEvent{0.0f, 1.0f},
        GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, true},
        GamepadButtonEvent{kGamepad0DeviceId, GamepadButton::A, false},
        GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickX, 0.5f},
        GamepadAxisEvent{kGamepad0DeviceId, GamepadAxis::LStickY, -0.5f},
        CharEvent{'a'},
        CharEvent{'b'},
        CharEvent{'c'},
        CharEvent{'d'},
    };

    auto pumpFrame = [&]() {
        backend.pushAll(slate, 16);
        ctx.beginFrame(1.0f / 60.0f);
        for (auto id : ids) (void)ctx.action(id);
        ctx.endFrame();
    };

    // Warmup.
    for (int i = 0; i < 100; ++i) pumpFrame();

    constexpr int kIters = 10000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) pumpFrame();
    const auto t1 = std::chrono::steady_clock::now();

    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    const double perFrameNs = static_cast<double>(ns) / kIters;
    const double perFrameUs = perFrameNs / 1000.0;

    std::printf("input_crowd_bench: 200 actions / 800 bindings / 16 events per frame\n");
    std::printf("                   %d iters, %.2f us / frame (gate < 50 us)\n",
                kIters, perFrameUs);
    std::printf("                   %s\n", perFrameUs < 50.0 ? "PASS" : "FAIL");
    return perFrameUs < 50.0 ? 0 : 1;
}
