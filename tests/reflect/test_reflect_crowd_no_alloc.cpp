/// @file test_reflect_crowd_no_alloc.cpp
/// @brief v1.0 close-out zero-allocation gate. After a 5-frame
/// warmup, 1000 `find` + `readField` + `enum_cast` calls per frame
/// across 100 frames must produce 0 allocs / 0 frees. A global
/// tracking new/delete pair counts everything that touches the heap
/// during the measured window.

#include "Check.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "threadmaxx_reflect/enum.hpp"
#include "threadmaxx_reflect/macro.hpp"
#include "threadmaxx_reflect/patch.hpp"
#include "threadmaxx_reflect/registry.hpp"

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

namespace {

struct Health { int current; int max; float regen; };
struct Velocity { float vx; float vy; float vz; };
struct Faction { int id; };

enum class Color : int {
    Red, Green, Blue, Cyan, Magenta, Yellow, White, Black,
};

THREADMAXX_REFLECT(Health,   current, max, regen);
THREADMAXX_REFLECT(Velocity, vx, vy, vz);
THREADMAXX_REFLECT(Faction,  id);

} // namespace

int main() {
    using namespace threadmaxx::reflect;

    // Register a few types up front (allocates).
    TypeRegistry reg;
    auto* h = reg.registerType<Health>("Health");
    auto* v = reg.registerType<Velocity>("Velocity");
    auto* f = reg.registerType<Faction>("Faction");
    CHECK(h && v && f);

    // Pre-warm the colour enum's lookup table (constexpr — usually
    // zero allocs but be safe).
    (void)enum_count<Color>();

    Health body{42, 100, 1.5f};

    auto perFrame = [&]() {
        for (int i = 0; i < 250; ++i) {
            (void)reg.find("Health");
            (void)reg.find("Velocity");
        }
        for (int i = 0; i < 250; ++i) {
            int out = 0;
            (void)h->findField("current");
            (void)h->fields[0].get<int>(&body);
            (void)out;
        }
        for (int i = 0; i < 250; ++i) {
            (void)enum_name(Color::Red);
            (void)enum_name(Color::Cyan);
        }
        for (int i = 0; i < 250; ++i) {
            // readField with a `std::string_view` literal — no alloc.
            auto rd = readField(h, &body, "current");
            (void)rd.ok();
        }
    };

    // 5-frame warmup.
    for (int i = 0; i < 5; ++i) perFrame();

    g_allocCount.store(0);
    g_freeCount.store(0);
    g_track.store(true);
    for (int i = 0; i < 100; ++i) perFrame();
    g_track.store(false);

    const auto allocs = g_allocCount.load();
    const auto frees  = g_freeCount.load();
    std::fprintf(stderr, "tracked frames=100 allocs=%llu frees=%llu\n",
                 static_cast<unsigned long long>(allocs),
                 static_cast<unsigned long long>(frees));
    CHECK_EQ(allocs, 0ull);
    CHECK_EQ(frees,  0ull);

    EXIT_WITH_RESULT();
}
