// AU6 — `resetPeaks()` clears peakL/peakR but preserves the cumulative
// `underruns` and `droppedVoices` counters.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    AudioMixer mixer(std::make_unique<LoopbackDevice>());
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 256;
    CHECK(mixer.initialize(fmt, bufFrames));

    std::vector<float> dc(bufFrames * 2, 0.8f);
    SoundId clip = mixer.addClip(dc, fmt);

    // Drive a non-zero peak via a clip voice.
    VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
    mixer.mix();
    CHECK(mixer.stats().peakL > 0.5f);

    // Drive an underrun via a starved stream.
    StreamId sid = mixer.addStream(std::make_unique<StarvedStream>(fmt));
    VoiceId sv   = mixer.play(VoiceDesc{ .stream = sid });
    mixer.mix();
    CHECK_EQ(mixer.stats().underruns, std::uint32_t{1});
    mixer.stop(sv);

    // Reset peaks.
    AudioDiagnostics diag(mixer);
    diag.resetPeaks();

    MixerStats s = mixer.stats();
    CHECK_EQ(s.peakL, 0.0f);
    CHECK_EQ(s.peakR, 0.0f);
    // Underrun + dropped counters are cumulative — reset doesn't touch them.
    CHECK_EQ(s.underruns, std::uint32_t{1});

    // Subsequent mix() repopulates peaks from the still-playing voice.
    mixer.mix();
    CHECK(mixer.stats().peakL > 0.5f);

    (void)v;
    EXIT_WITH_RESULT();
}
