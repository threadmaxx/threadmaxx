// v1.0 close-out gate — the voice pool scales to 256 simultaneously-playing
// voices without allocation. Same global-new tracking shape as
// `test_audio_mixer_no_allocations`, but with `MaxVoices = 256` and the
// pool fully saturated before tracking begins.

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

    auto deviceOwner    = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();

    AudioMixerConfig cfg{};
    cfg.maxVoices = 256;
    cfg.maxBuses  = 4;
    cfg.maxClips  = 4;
    AudioMixer mixer(std::move(deviceOwner), cfg);
    CHECK(mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 1024));

    // Warmup: clip + bus + saturate the voice pool with looping voices.
    std::vector<float> dc(1024 * 2, 0.05f);
    SoundId clip = mixer.addClip(dc, AudioFormat{48000, 2, ChannelLayout::Stereo});
    CHECK(clip.value != 0);
    BusId routing = mixer.createBus({});
    CHECK(routing.value != 0);

    for (std::uint32_t i = 0; i < cfg.maxVoices; ++i) {
        VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .bus = routing, .looping = true });
        CHECK(v.value != 0);
    }
    CHECK_EQ(mixer.stats().activeVoices, cfg.maxVoices);
    mixer.mix();

    // Drop capture, then run 100 mix cycles under the tracking allocator.
    dev->setCaptureEnabled(false);

    g_allocCount.store(0, std::memory_order_relaxed);
    g_tracking.store(true, std::memory_order_relaxed);
    for (int i = 0; i < 100; ++i) {
        mixer.mix();
    }
    g_tracking.store(false, std::memory_order_relaxed);

    CHECK_EQ(g_allocCount.load(std::memory_order_relaxed), std::size_t{0});
    CHECK_EQ(dev->droppedSubmits(), std::size_t{100});
    CHECK_EQ(mixer.stats().activeVoices, cfg.maxVoices);

    EXIT_WITH_RESULT();
}
