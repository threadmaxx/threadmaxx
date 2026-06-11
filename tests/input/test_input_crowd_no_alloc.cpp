/// @file test_input_crowd_no_alloc.cpp
/// @brief v1.0 close-out zero-allocation gate. 500 actions / 2000 bindings
/// / 8 connected gamepads / 16 events per frame / 100 measured frames
/// after a 5-frame warmup. Tracking allocator confirms zero heap traffic.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <string>

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/binding.hpp"
#include "threadmaxx_input/context.hpp"

namespace {

std::atomic<bool> g_track{false};
std::atomic<std::uint64_t> g_allocCount{0};
std::atomic<std::uint64_t> g_freeCount{0};

}  // namespace

void* operator new(std::size_t n) {
    if (g_track.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t n) {
    if (g_track.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}
void operator delete[](void* p) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    if (p && g_track.load(std::memory_order_relaxed)) {
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

int main() {
    using namespace threadmaxx::input;

    NullBackend backend;
    backend.reserve(1024);
    InputContext ctx;
    ctx.setBackend(&backend);
    ctx.reserveEvents(1024);

    // 500 actions × 4 bindings = 2000 bindings.
    std::vector<ActionId> ids;
    ids.reserve(500);
    for (int i = 0; i < 500; ++i) {
        ids.push_back(actionId(std::string("Crowd_") + std::to_string(i)));
    }
    BindingSet bs;
    for (int i = 0; i < 500; ++i) {
        const auto idx = static_cast<std::uint16_t>(i % 26);
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::key(static_cast<Key>(static_cast<std::uint16_t>(Key::A) + idx)));
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::mouseButton(static_cast<MouseButton>(i % 5)));
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::gamepadButton(static_cast<GamepadButton>(i % 15),
                                       gamepadDeviceId(static_cast<std::uint16_t>(i % 8))));
        bs.bind(ids[static_cast<std::size_t>(i)],
                Binding::gamepadAxisPositive(static_cast<GamepadAxis>(i % 6), 0.3f,
                                             gamepadDeviceId(static_cast<std::uint16_t>(i % 8))));
    }
    ctx.setBindings(bs);

    // 8 gamepads pre-connected.
    for (std::uint16_t pad = 0; pad < 8; ++pad) {
        backend.push(DeviceConnectEvent{gamepadDeviceId(pad), true});
    }

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
        for (std::uint16_t pad = 0; pad < 8; ++pad) {
            (void)ctx.stickXY(gamepadDeviceId(pad), Stick::Left);
            (void)ctx.trigger(gamepadDeviceId(pad), Trigger::Right);
        }
        ctx.endFrame();
    };

    // Warmup.
    for (int i = 0; i < 5; ++i) pumpFrame();

    g_allocCount.store(0, std::memory_order_relaxed);
    g_freeCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (int i = 0; i < 100; ++i) pumpFrame();

    g_track.store(false, std::memory_order_relaxed);

    const auto allocs = g_allocCount.load(std::memory_order_relaxed);
    const auto frees = g_freeCount.load(std::memory_order_relaxed);
    if (allocs != 0 || frees != 0) {
        std::fprintf(stderr, "v1.0 crowd no-alloc gate broke: allocs=%llu frees=%llu\n",
                     static_cast<unsigned long long>(allocs),
                     static_cast<unsigned long long>(frees));
    }
    CHECK_EQ(allocs, std::uint64_t{0});
    CHECK_EQ(frees, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
