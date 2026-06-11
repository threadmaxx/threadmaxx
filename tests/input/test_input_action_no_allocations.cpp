/// @file test_input_action_no_allocations.cpp
/// @brief 200 actions / 800 bindings pre-bound; 100 frames of action
/// queries produce zero heap traffic under a tracking allocator.

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
    InputContext ctx;
    ctx.setBackend(&backend);
    ctx.reserveEvents(256);

    // Pre-allocate 200 action IDs.
    std::vector<ActionId> ids;
    ids.reserve(200);
    for (int i = 0; i < 200; ++i) {
        const auto name = std::string("Action_") + std::to_string(i);
        ids.push_back(actionId(name));
    }

    // Build 200 actions × 4 bindings = 800 bindings.
    BindingSet bs;
    for (int i = 0; i < 200; ++i) {
        const auto idx = static_cast<std::uint16_t>(i % 26);
        bs.bind(ids[static_cast<std::size_t>(i)], Binding::key(static_cast<Key>(static_cast<std::uint16_t>(Key::A) + idx)));
        bs.bind(ids[static_cast<std::size_t>(i)], Binding::mouseButton(static_cast<MouseButton>(i % 5)));
        bs.bind(ids[static_cast<std::size_t>(i)], Binding::gamepadButton(static_cast<GamepadButton>(i % 15)));
        bs.bind(ids[static_cast<std::size_t>(i)], Binding::gamepadAxisPositive(static_cast<GamepadAxis>(i % 6), 0.4f));
    }
    ctx.setBindings(bs);

    auto pumpFrame = [&]() {
        ctx.beginFrame(1.0f / 60.0f);
        // Query every action once per frame.
        for (auto id : ids) (void)ctx.action(id);
        ctx.endFrame();
    };

    // Warmup.
    for (int i = 0; i < 3; ++i) pumpFrame();

    g_allocCount.store(0, std::memory_order_relaxed);
    g_freeCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    for (int i = 0; i < 100; ++i) pumpFrame();

    g_track.store(false, std::memory_order_relaxed);

    const std::uint64_t allocs = g_allocCount.load(std::memory_order_relaxed);
    const std::uint64_t frees = g_freeCount.load(std::memory_order_relaxed);

    if (allocs != 0 || frees != 0) {
        std::fprintf(stderr, "action no-alloc gate broke: allocs=%llu frees=%llu\n",
                     static_cast<unsigned long long>(allocs),
                     static_cast<unsigned long long>(frees));
    }
    CHECK_EQ(allocs, std::uint64_t{0});
    CHECK_EQ(frees, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
