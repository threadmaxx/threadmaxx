/// @file test_ui_widget_no_allocations.cpp
/// @brief 50-widget panel, 100 frames after warmup, zero heap traffic.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

#include "threadmaxx_ui/backends/NullBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace {
std::atomic<bool> g_track{false};
std::atomic<std::uint64_t> g_allocCount{0};
std::atomic<std::uint64_t> g_freeCount{0};
} // namespace

void* operator new(std::size_t n) {
    if (g_track.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t n) {
    if (g_track.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept {
    if (p && g_track.load(std::memory_order_relaxed))
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete[](void* p) noexcept {
    if (p && g_track.load(std::memory_order_relaxed))
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    if (p && g_track.load(std::memory_order_relaxed))
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    if (p && g_track.load(std::memory_order_relaxed))
        g_freeCount.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    NullBackend backend;
    ctx.setBackend(&backend);
    ctx.reserveHitTests(64);
    ctx.reserveWidgetStates(64);

    bool boolVals[50] = {};
    float floatVals[50] = {};

    auto frame = [&](const UIInput& input) {
        ctx.setInput(input);
        ctx.beginFrame();
        for (int i = 0; i < 50; ++i) {
            const WidgetID id{static_cast<std::uint64_t>(0x1000 + i)};
            const Rect r{0, i * 22, 200, 20};
            if ((i % 4) == 0) checkbox(ctx, id, r, "label", &boolVals[i]);
            else if ((i % 4) == 1) button(ctx, id, r, "btn");
            else if ((i % 4) == 2) slider(ctx, id, r, &floatVals[i], 0.0f, 1.0f);
            else                   selectable(ctx, id, r, "row", false);
        }
        ctx.endFrame();
    };

    UIInput none;
    for (int i = 0; i < 5; ++i) frame(none);

    g_allocCount.store(0, std::memory_order_relaxed);
    g_freeCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (int i = 0; i < 100; ++i) frame(none);

    g_track.store(false, std::memory_order_relaxed);

    const auto allocs = g_allocCount.load(std::memory_order_relaxed);
    const auto frees  = g_freeCount.load(std::memory_order_relaxed);
    if (allocs != 0 || frees != 0) {
        std::fprintf(stderr, "widget no-alloc gate broke: allocs=%llu frees=%llu\n",
                     static_cast<unsigned long long>(allocs),
                     static_cast<unsigned long long>(frees));
    }
    CHECK_EQ(allocs, std::uint64_t{0});
    CHECK_EQ(frees, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
