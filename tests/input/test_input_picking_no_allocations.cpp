/// @file test_input_picking_no_allocations.cpp
/// @brief 1000 ray builds under a tracking allocator yield zero heap
/// traffic. screenToRay / worldToScreen only touch stack-locals.

#include "Check.hpp"

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <new>

#include "picking_test_helpers.hpp"

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

    Camera cam{};
    const float eye[3] = {0.0f, 5.0f, -10.0f};
    const float target[3] = {0.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    test::lookAt(eye, target, up, cam.view);
    test::perspectiveVulkan(1.0f, 1280.0f / 720.0f, 0.1f, 100.0f, cam.projection);
    cam.viewportW = 1280.0f;
    cam.viewportH = 720.0f;

    g_allocCount.store(0, std::memory_order_relaxed);
    g_freeCount.store(0, std::memory_order_relaxed);
    g_track.store(true, std::memory_order_relaxed);

    volatile float acc = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        const float sx = static_cast<float>(i % 1280);
        const float sy = static_cast<float>(i % 720);
        const Ray r = screenToRay(cam, sx, sy);
        const float p[3] = {static_cast<float>(i % 10),
                            static_cast<float>(i % 7),
                            static_cast<float>(i % 13)};
        const auto sp = worldToScreen(cam, p);
        acc += r.direction[0] + sp.x;
    }
    (void)acc;

    g_track.store(false, std::memory_order_relaxed);

    const auto allocs = g_allocCount.load(std::memory_order_relaxed);
    const auto frees = g_freeCount.load(std::memory_order_relaxed);
    if (allocs != 0 || frees != 0) {
        std::fprintf(stderr, "picking no-alloc gate broke: allocs=%llu frees=%llu\n",
                     static_cast<unsigned long long>(allocs),
                     static_cast<unsigned long long>(frees));
    }
    CHECK_EQ(allocs, std::uint64_t{0});
    CHECK_EQ(frees, std::uint64_t{0});

    EXIT_WITH_RESULT();
}
