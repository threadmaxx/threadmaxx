/// @file test_input_trace_no_allocations.cpp
/// @brief Replay path is zero-alloc after warmup. The recording path is
/// allowed to allocate (it grows a per-frame vector); replay just shuttles
/// already-recorded bytes through the backend queue.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <new>

#include "threadmaxx_input/backends/NullBackend.hpp"
#include "threadmaxx_input/context.hpp"
#include "threadmaxx_input/trace.hpp"

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

    // Build a 100-frame trace (recording is permitted to allocate).
    InputTrace trace;
    for (int i = 0; i < 100; ++i) {
        trace.appendFrame(std::vector<InputEvent>{
            KeyEvent{Key::W, Modifiers::None, (i & 1) == 0},
            MouseMoveEvent{static_cast<float>(i), 0.0f, 1.0f, 0.0f},
            MouseButtonEvent{MouseButton::Left, (i & 1) == 0, 0.0f, 0.0f},
            CharEvent{'a'},
        });
    }

    // Warmup the replay-side backend + context.
    NullBackend backend;
    backend.reserve(64);
    InputContext ctx;
    ctx.setBackend(&backend);
    ctx.reserveEvents(64);

    auto pumpReplay = [&](std::uint64_t i) {
        trace.replayTo(backend, i);
        ctx.beginFrame(1.0f / 60.0f);
        ctx.endFrame();
    };

    for (std::uint64_t i = 0; i < 3; ++i) pumpReplay(i);

    g_allocCount.store(0, std::memory_order_relaxed);
    g_freeCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (std::uint64_t i = 3; i < 100; ++i) pumpReplay(i);

    g_track.store(false, std::memory_order_relaxed);

    const auto allocs = g_allocCount.load(std::memory_order_relaxed);
    const auto frees = g_freeCount.load(std::memory_order_relaxed);
    if (allocs != 0 || frees != 0) {
        std::fprintf(stderr, "trace no-alloc gate broke: allocs=%llu frees=%llu\n",
                     static_cast<unsigned long long>(allocs),
                     static_cast<unsigned long long>(frees));
    }
    CHECK_EQ(allocs, std::uint64_t{0});
    CHECK_EQ(frees, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
