// AU5 — every DSP helper is zero-alloc by construction; this test pins
// that contract under a tracking allocator.

#include "Check.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

namespace {
std::atomic<std::size_t> g_allocCount{0};
std::atomic<bool>        g_tracking{false};
}

void* operator new(std::size_t n) {
    if (g_tracking.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void* operator new[](std::size_t n) {
    if (g_tracking.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#include "threadmaxx_audio/threadmaxx_audio.hpp"

int main() {
    using namespace threadmaxx::audio;

    AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    std::vector<float> buf(1024 * 2, 0.5f);
    AudioSpan span{ buf.data(), 1024, fmt };

    // Begin tracking — every helper hereafter must allocate zero bytes.
    g_allocCount.store(0, std::memory_order_relaxed);
    g_tracking.store(true, std::memory_order_relaxed);

    for (int i = 0; i < 64; ++i) {
        applyGain(span, -3.0f);
        applyPanStereo(span, 0.0f);
        applyFadeIn(span,  0.01f, 48000.0f);
        applyFadeOut(span, 0.01f, 48000.0f);
    }

    g_tracking.store(false, std::memory_order_relaxed);
    CHECK_EQ(g_allocCount.load(std::memory_order_relaxed), std::size_t{0});

    EXIT_WITH_RESULT();
}
