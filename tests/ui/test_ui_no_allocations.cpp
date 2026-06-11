/// @file test_ui_no_allocations.cpp
/// @brief Pins the zero-allocation contract on the frame hot path. After
/// three warmup frames (lets the draw list grow to a steady-state shape),
/// 100 subsequent frames must produce zero heap allocations.
///
/// Uses a global `operator new` override that increments an atomic counter
/// while a gate flag is on. Same pattern as `test_audio_mixer_no_allocations`.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

#include "threadmaxx_ui/backends/NullBackend.hpp"
#include "threadmaxx_ui/context.hpp"

namespace {

std::atomic<bool> g_track{false};
std::atomic<std::uint64_t> g_allocCount{0};
std::atomic<std::uint64_t> g_freeCount{0};

} // namespace

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
    using namespace threadmaxx::ui;

    NullBackend backend;
    UIContext ctx;
    ctx.setBackend(&backend);

    auto pumpFrame = [&](int rectCount, int textCount) {
        ctx.beginFrame();
        auto& dl = ctx.drawList();
        for (int i = 0; i < rectCount; ++i) {
            dl.emitRect(Rect{0, 0, 100, 100}, Color{255, 0, 0, 255});
        }
        for (int i = 0; i < textCount; ++i) {
            dl.emitText(Vec2i{0, 0}, Color{255, 255, 255, 255}, "warmup ABCDEF");
        }
        ctx.endFrame();
    };

    // 3 warmup frames — let the draw list grow its vectors to the
    // steady-state size for this workload.
    for (int i = 0; i < 3; ++i) {
        pumpFrame(64, 16);
    }

    // Tracking on.
    g_allocCount.store(0, std::memory_order_relaxed);
    g_freeCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (int i = 0; i < 100; ++i) {
        pumpFrame(64, 16);
    }

    g_track.store(false, std::memory_order_relaxed);

    const std::uint64_t allocs = g_allocCount.load(std::memory_order_relaxed);
    const std::uint64_t frees  = g_freeCount.load(std::memory_order_relaxed);

    if (allocs != 0 || frees != 0) {
        std::fprintf(stderr, "no-alloc gate broke: allocs=%llu frees=%llu\n",
                     static_cast<unsigned long long>(allocs),
                     static_cast<unsigned long long>(frees));
    }
    CHECK_EQ(allocs, std::uint64_t{0});
    CHECK_EQ(frees,  std::uint64_t{0});

    EXIT_WITH_RESULT();
}
