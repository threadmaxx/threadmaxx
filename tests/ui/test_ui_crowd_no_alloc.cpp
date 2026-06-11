/// @file test_ui_crowd_no_alloc.cpp
/// @brief v1.0 close-out gate: 500 widgets across 8 panels, 100 frames
/// after warmup, zero heap traffic under the tracking allocator.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

#include "threadmaxx_ui/backends/VertexBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"
#include "threadmaxx_ui/panel.hpp"
#include "threadmaxx_ui/widget.hpp"

namespace {
std::atomic<bool> g_track{false};
std::atomic<std::uint64_t> g_allocCount{0};
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
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    VertexBackend backend;
    backend.reserve(32768, 4096);
    ctx.setBackend(&backend);
    ctx.reserveHitTests(2048);
    ctx.reserveWidgetStates(2048);
    setHostRect(ctx, Rect{0, 0, 1920, 1080});

    constexpr int kPanels = 8;
    constexpr int kWidgetsPerPanel = 64;  // 512 widgets total — meets the
                                          // "500 widgets across 8 panels"
                                          // gate from FUTURE_WORK.
    PanelState panels[kPanels];
    for (int p = 0; p < kPanels; ++p) {
        panels[p].bounds = Rect{(p % 4) * 480, (p / 4) * 540, 470, 530};
    }
    bool boolVals[kPanels * kWidgetsPerPanel] = {};
    float floatVals[kPanels * kWidgetsPerPanel] = {};

    auto frame = [&]() {
        UIInput none;
        ctx.setInput(none);
        ctx.beginFrame();
        for (int p = 0; p < kPanels; ++p) {
            const WidgetID panelId{0xA000 + static_cast<std::uint64_t>(p)};
            if (beginPanel(ctx, panelId, "Panel", panels[p])) {
                for (int w = 0; w < kWidgetsPerPanel; ++w) {
                    const int idx = p * kWidgetsPerPanel + w;
                    const Rect row{
                        panels[p].bounds.x + 8,
                        panels[p].bounds.y + 24 + w * 8,
                        panels[p].bounds.w - 16, 8};
                    if ((w & 1) == 0) {
                        inspect(ctx,
                                WidgetID{static_cast<std::uint64_t>(0x10000 + idx)},
                                row, "b", &boolVals[idx]);
                    } else {
                        inspect(ctx,
                                WidgetID{static_cast<std::uint64_t>(0x20000 + idx)},
                                row, "f", &floatVals[idx]);
                    }
                }
                endPanel(ctx);
            }
        }
        ctx.endFrame();
    };

    // Warmup primes hit-tests, widget-state map, draw list, vertex
    // backend.
    for (int i = 0; i < 8; ++i) frame();

    g_allocCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (int i = 0; i < 100; ++i) frame();

    g_track.store(false, std::memory_order_relaxed);

    const auto allocs = g_allocCount.load(std::memory_order_relaxed);
    if (allocs != 0) {
        std::fprintf(stderr,
                     "crowd no-alloc gate broke: allocs=%llu\n",
                     static_cast<unsigned long long>(allocs));
    }
    CHECK_EQ(allocs, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
