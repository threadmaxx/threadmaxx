// AU7 / v1.0 close-out — `audio_crowd_bench`: many simultaneously-playing
// voices spread across multiple buses. Reports avg mix cost per
// `bufferFrames` buffer. The v1.0 close-out gate is "< 2 ms per 1024 frames
// at 48 kHz for 256 voices."
//
// Usage:
//   ./audio_crowd_bench [voices=512] [buses=4] [iterations=2000]
//
// Each voice plays a short looping DC clip routed to a random bus; the
// bench drains the LoopbackDevice's capture buffer between iterations so
// the loopback's bookkeeping doesn't skew the timing.

#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace threadmaxx::audio;

    std::uint32_t voiceCount = (argc > 1) ? static_cast<std::uint32_t>(std::stoul(argv[1])) : 512u;
    std::uint32_t busCount   = (argc > 2) ? static_cast<std::uint32_t>(std::stoul(argv[2])) :   4u;
    std::size_t   iterations = (argc > 3) ? static_cast<std::size_t>(std::stoul(argv[3]))   : 2000u;

    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufferFrames = 1024;

    auto deviceOwner   = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();

    AudioMixerConfig cfg{};
    cfg.maxVoices = voiceCount;
    cfg.maxBuses  = busCount + 4u;  // master + user buses + headroom
    cfg.maxClips  = 8u;
    AudioMixer mixer(std::move(deviceOwner), cfg);
    if (!mixer.initialize(fmt, bufferFrames)) {
        std::fprintf(stderr, "mixer.initialize failed\n");
        return 1;
    }

    // Tiny looping clip — keeps each per-voice copy O(bufferFrames).
    std::vector<float> dc(bufferFrames * 2, 0.05f);
    SoundId clip = mixer.addClip(dc, fmt);

    std::vector<BusId> buses;
    buses.reserve(busCount);
    for (std::uint32_t i = 0; i < busCount; ++i) {
        buses.push_back(mixer.createBus({}));
    }

    for (std::uint32_t i = 0; i < voiceCount; ++i) {
        VoiceDesc d{};
        d.sound   = clip;
        d.bus     = buses[i % busCount];
        d.looping = true;
        if (mixer.play(d).value == 0) {
            std::fprintf(stderr, "play failed at voice %u (pool exhausted)\n", i);
            return 1;
        }
    }

    // Disable capture: each submit emplace_back/assign on the loopback would
    // otherwise add ~constant per-iteration overhead unrelated to mix cost.
    dev->setCaptureEnabled(false);

    // Warmup.
    for (std::size_t i = 0; i < 64; ++i) mixer.mix();

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    for (std::size_t i = 0; i < iterations; ++i) mixer.mix();
    const auto t1 = clock::now();

    const double elapsedUs = std::chrono::duration<double, std::micro>(t1 - t0).count();
    const double perCallUs = elapsedUs / static_cast<double>(iterations);
    const double perCallMs = perCallUs / 1000.0;
    const double voiceUs   = perCallUs / static_cast<double>(voiceCount);

    std::printf("audio_crowd_bench: voices=%u buses=%u iters=%zu buf=%zu @ %u Hz\n",
                voiceCount, busCount, iterations, bufferFrames, fmt.sampleRate);
    std::printf("  avg mix cost: %.3f ms/buffer  (%.3f us/voice)\n",
                perCallMs, voiceUs);
    std::printf("  v1.0 gate: < 2.000 ms/buffer @ 256 voices\n");
    if (voiceCount >= 256 && perCallMs >= 2.0) {
        std::printf("  GATE FAIL\n");
        return 2;
    }
    std::printf("  GATE PASS\n");
    return 0;
}
