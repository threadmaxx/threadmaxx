/// @file test_ui_inspect_no_allocations.cpp
/// @brief 20 mixed-type inspector rows, 100 frames, zero heap traffic.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <new>
#include <utility>
#include <span>

#include "threadmaxx_ui/backends/NullBackend.hpp"
#include "threadmaxx_ui/context.hpp"
#include "threadmaxx_ui/input.hpp"
#include "threadmaxx_ui/inspect.hpp"

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

enum class Mode { A, B, C };
static const std::pair<Mode, std::string_view> kOpts[3] = {
    {Mode::A, "A"}, {Mode::B, "B"}, {Mode::C, "C"}};

int main() {
    using namespace threadmaxx::ui;

    UIContext ctx;
    NullBackend backend;
    ctx.setBackend(&backend);
    ctx.reserveHitTests(128);
    ctx.reserveWidgetStates(128);

    bool b[5]{};
    std::int32_t i[5]{};
    float f[5]{};
    Mode m[5]{};

    auto frame = [&](const UIInput& in) {
        ctx.setInput(in);
        ctx.beginFrame();
        for (int k = 0; k < 5; ++k) {
            const Rect r{0, k * 22, 300, 20};
            inspect(ctx, WidgetID{0x1000 + static_cast<std::uint64_t>(k)},
                    r, "bool", &b[k]);
        }
        for (int k = 0; k < 5; ++k) {
            const Rect r{0, (5 + k) * 22, 300, 20};
            inspect(ctx, WidgetID{0x2000 + static_cast<std::uint64_t>(k)},
                    r, "int", &i[k]);
        }
        for (int k = 0; k < 5; ++k) {
            const Rect r{0, (10 + k) * 22, 300, 20};
            inspect(ctx, WidgetID{0x3000 + static_cast<std::uint64_t>(k)},
                    r, "float", &f[k]);
        }
        for (int k = 0; k < 5; ++k) {
            const Rect r{0, (15 + k) * 22, 300, 20};
            inspectEnum(ctx, WidgetID{0x4000 + static_cast<std::uint64_t>(k)},
                        r, "mode", &m[k],
                        std::span<const std::pair<Mode, std::string_view>>{kOpts, 3});
        }
        ctx.endFrame();
    };

    UIInput none;
    for (int w = 0; w < 5; ++w) frame(none);

    g_allocCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (int w = 0; w < 100; ++w) frame(none);

    g_track.store(false, std::memory_order_relaxed);

    const auto allocs = g_allocCount.load(std::memory_order_relaxed);
    if (allocs != 0) {
        std::fprintf(stderr, "inspector no-alloc gate broke: allocs=%llu\n",
                     static_cast<unsigned long long>(allocs));
    }
    CHECK_EQ(allocs, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
