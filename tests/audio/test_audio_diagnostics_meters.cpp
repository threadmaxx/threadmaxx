// AU6 — play a known DC clip; peak meter reports ~1.0 within one mix call.
// Also verify RMS is non-zero on a non-silent buffer.

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

    // DC clip at 1.0 amplitude on both channels — peak meter should reach
    // 1.0 ± float tolerance.
    std::vector<float> dc(bufFrames * 2, 1.0f);
    SoundId clip = mixer.addClip(dc, fmt);

    AudioDiagnostics diag(mixer);

    // Before any mix, all meters at zero.
    {
        MixerStats s = diag.stats();
        CHECK_EQ(s.peakL, 0.0f);
        CHECK_EQ(s.peakR, 0.0f);
        CHECK_EQ(s.rmsL,  0.0f);
        CHECK_EQ(s.rmsR,  0.0f);
    }

    VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
    mixer.mix();

    MixerStats s = diag.stats();
    CHECK(std::fabs(s.peakL - 1.0f) < 1e-3f);
    CHECK(std::fabs(s.peakR - 1.0f) < 1e-3f);
    // RMS of a constant DC signal of amplitude A is A itself.
    CHECK(std::fabs(s.rmsL - 1.0f) < 1e-3f);
    CHECK(std::fabs(s.rmsR - 1.0f) < 1e-3f);

    // Lower-amplitude signal: peak hold preserves previous peak (1.0); RMS
    // tracks the current buffer.
    mixer.stop(v);
    std::vector<float> halfDc(bufFrames * 2, 0.5f);
    SoundId quietClip = mixer.addClip(halfDc, fmt);
    VoiceId v2 = mixer.play(VoiceDesc{ .sound = quietClip, .looping = true });
    mixer.mix();

    MixerStats s2 = diag.stats();
    CHECK(std::fabs(s2.peakL - 1.0f) < 1e-3f); // peak is hold-max from earlier
    CHECK(std::fabs(s2.rmsL - 0.5f) < 1e-3f);  // RMS reflects current
    (void)v2;

    EXIT_WITH_RESULT();
}
