// AU3 — register a noise stream, play it, mix 10 seconds at 48kHz; the
// stream cursor matches the cumulative frame count and the captured output
// is above silence.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <memory>

static float peakAbs(const float* data, std::size_t n) {
    float peak = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float a = data[i] < 0.0f ? -data[i] : data[i];
        if (a > peak) peak = a;
    }
    return peak;
}

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();
    AudioMixer mixer(std::move(deviceOwner));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 1024;
    CHECK(mixer.initialize(fmt, bufFrames));

    // Infinite NoiseStream — never finishes; the mixer reads exactly
    // bufferFrames per mix() call.
    auto streamOwner   = std::make_unique<NoiseStream>(fmt /* totalFrames=0 → infinite */);
    NoiseStream* nptr  = streamOwner.get();
    StreamId sid       = mixer.addStream(std::move(streamOwner));
    CHECK(sid.value != 0);
    CHECK(mixer.isValidStream(sid));

    VoiceId v = mixer.play(VoiceDesc{ .stream = sid });
    CHECK(v.value != 0);
    CHECK(mixer.isPlaying(v));

    // Mix 10 seconds @ 48kHz with 1024-frame buffers → 469 calls covers
    // ~9.999 sec; round up to 470 for a clean ≥ 10 sec gate.
    constexpr std::size_t kSeconds = 10;
    const std::size_t totalFrames  = kSeconds * fmt.sampleRate;
    const std::size_t calls        = (totalFrames + bufFrames - 1) / bufFrames;
    for (std::size_t i = 0; i < calls; ++i) mixer.mix();

    CHECK_EQ(nptr->cursor(), calls * bufFrames);
    CHECK(!nptr->finished());

    // Loopback captured one buffer per mix.
    CHECK_EQ(dev->capturedBuffers().size(), calls);

    // Noise is loud; the last captured buffer's peak is well above silence.
    const auto& last = dev->capturedBuffers().back();
    CHECK(peakAbs(last.data(), last.size()) > 0.1f);

    // No producer underruns from a healthy infinite stream.
    CHECK_EQ(mixer.stats().underruns, std::uint32_t{0});

    EXIT_WITH_RESULT();
}
