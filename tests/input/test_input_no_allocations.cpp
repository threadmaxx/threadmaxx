/// @file test_input_no_allocations.cpp
/// @brief Pins the zero-allocation contract on the hot path. After three
/// warmup frames (lets the event drain buffer grow to steady-state shape),
/// 100 subsequent frames driven by a 16-event-per-frame synthetic stream
/// must produce zero heap allocations.
///
/// Uses a global `operator new` override that increments an atomic counter
/// while a gate flag is on. Same pattern as `test_ui_no_allocations`.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

#include "threadmaxx_input/backends/NullBackend.hpp"
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
    backend.reserve(256);
    InputContext ctx;
    ctx.setBackend(&backend);
    ctx.reserveEvents(256);

    // Build a fixed 16-event slate the host re-pushes every frame.
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
        // Drive the queries the host would run.
        (void)ctx.isHeld(Key::A);
        (void)ctx.wasPressed(Key::A);
        (void)ctx.wasReleased(Key::B);
        (void)ctx.isHeld(MouseButton::Left);
        (void)ctx.axis(kGamepad0DeviceId, GamepadAxis::LStickX);
        ctx.endFrame();
    };

    // Warmup — lets the event drain buffer + backend queue stabilise.
    for (int i = 0; i < 3; ++i) pumpFrame();

    g_allocCount.store(0, std::memory_order_relaxed);
    g_freeCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (int i = 0; i < 100; ++i) pumpFrame();

    g_track.store(false, std::memory_order_relaxed);

    const std::uint64_t allocs = g_allocCount.load(std::memory_order_relaxed);
    const std::uint64_t frees = g_freeCount.load(std::memory_order_relaxed);

    if (allocs != 0 || frees != 0) {
        std::fprintf(stderr, "no-alloc gate broke: allocs=%llu frees=%llu\n",
                     static_cast<unsigned long long>(allocs),
                     static_cast<unsigned long long>(frees));
    }
    CHECK_EQ(allocs, std::uint64_t{0});
    CHECK_EQ(frees, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
