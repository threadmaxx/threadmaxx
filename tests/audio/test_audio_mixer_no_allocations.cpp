// AU2 contract — under a tracking allocator, 100 mix-cycles after warmup
// produce zero heap allocations. This pins the hot path against future
// regressions: a hidden std::function capture, a vector growth, anything
// allocating in mix() will trip the counter.
//
// Implementation note: we override the global operator new / delete pair and
// flip a gate on/off around the measurement. The LoopbackDevice's per-submit
// capture is disabled after warmup so the captured-buffer storage doesn't
// alloc on every mix().

#include "Check.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <memory>
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

    auto deviceOwner   = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();

    AudioMixerConfig cfg{};
    cfg.maxVoices = 8;
    cfg.maxBuses  = 4;
    cfg.maxClips  = 8;
    AudioMixer mixer(std::move(deviceOwner), cfg);
    CHECK(mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 256));

    // Warmup: add clip, create bus, fill voice pool with looping voices,
    // do one mix() so every cold-path lazy allocation is paid up front.
    std::vector<float> dc(256 * 2, 0.25f);
    SoundId clip = mixer.addClip(dc, AudioFormat{48000, 2, ChannelLayout::Stereo});
    CHECK(clip.value != 0);

    BusId routing = mixer.createBus({});
    CHECK(routing.value != 0);

    for (std::uint32_t i = 0; i < cfg.maxVoices; ++i) {
        VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .bus = routing, .looping = true });
        CHECK(v.value != 0);
    }
    mixer.mix();

    // Stop capture: subsequent submits skip the per-call emplace_back.
    dev->setCaptureEnabled(false);

    // Begin tracking and run 100 mix cycles.
    g_allocCount.store(0, std::memory_order_relaxed);
    g_tracking.store(true, std::memory_order_relaxed);
    for (int i = 0; i < 100; ++i) {
        mixer.mix();
    }
    g_tracking.store(false, std::memory_order_relaxed);

    CHECK_EQ(g_allocCount.load(std::memory_order_relaxed), std::size_t{0});

    // Sanity: the dropped-submit counter shows mix() actually ran.
    CHECK_EQ(dev->droppedSubmits(), std::size_t{100});

    EXIT_WITH_RESULT();
}
