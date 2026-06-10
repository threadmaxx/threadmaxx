// AU3 — a stream that returns fewer frames than requested (and is NOT
// finished) triggers `MixerStats::underruns++` and the mixer emits silence
// for the missing frames.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <memory>

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner   = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();
    AudioMixer mixer(std::move(deviceOwner));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 1024;
    CHECK(mixer.initialize(fmt, bufFrames));

    // StarvedStream writes exactly bufFrames/2 frames of 0.25 each call;
    // never finished.
    auto streamOwner = std::make_unique<StarvedStream>(fmt);
    StreamId sid     = mixer.addStream(std::move(streamOwner));

    VoiceId v = mixer.play(VoiceDesc{ .stream = sid });
    CHECK(v.value != 0);

    mixer.mix();

    // Underrun counted exactly once.
    CHECK_EQ(mixer.stats().underruns, std::uint32_t{1});

    // Captured output: first (bufFrames/2) * channels samples are 0.25,
    // remainder are zero (silence fill for the short read).
    const auto& buf = dev->capturedBuffers().back();
    CHECK_EQ(buf.size(), samplesIn(fmt, bufFrames));

    const std::size_t firstHalfSamples = (bufFrames / 2u) * fmt.channels;
    for (std::size_t i = 0; i < firstHalfSamples; ++i) {
        CHECK(std::fabs(buf[i] - 0.25f) < 1e-5f);
    }
    for (std::size_t i = firstHalfSamples; i < buf.size(); ++i) {
        CHECK_EQ(buf[i], 0.0f);
    }

    // The voice keeps playing (not finished, not stopped) — the underrun
    // doesn't tear it down.
    CHECK(mixer.isPlaying(v));

    // Second mix call: another underrun.
    mixer.mix();
    CHECK_EQ(mixer.stats().underruns, std::uint32_t{2});

    EXIT_WITH_RESULT();
}
