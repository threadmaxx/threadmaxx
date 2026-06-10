// AU4 — emitter at `maxDistance` produces silence; at `minDistance` produces
// full gain; midpoint follows the configured Linear curve.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

static float peakAbs(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float v : buf) {
        const float a = v < 0.0f ? -v : v;
        if (a > peak) peak = a;
    }
    return peak;
}

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner   = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();
    AudioMixer mixer(std::move(deviceOwner));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 256;
    CHECK(mixer.initialize(fmt, bufFrames));

    // DC clip — constant 0.5 in both channels. After mono down-mix in the
    // spatial path, each frame sample is 0.5.
    std::vector<float> dc(bufFrames * 2, 0.5f);
    SoundId clip = mixer.addClip(dc, fmt);

    // Listener at origin facing +Z (forward), +Y up.
    ListenerDesc l{};
    l.position = { 0.0f, 0.0f, 0.0f };
    l.forward  = { 0.0f, 0.0f, 1.0f };
    l.up       = { 0.0f, 1.0f, 0.0f };
    ListenerId lid = mixer.createListener(l);
    CHECK(lid.value != 0);

    auto playAt = [&](Vec3 pos, float minD, float maxD) -> float {
        VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
        EmitterDesc e{};
        e.position         = pos;
        e.minDistance      = minD;
        e.maxDistance      = maxD;
        e.dopplerFactor    = 0.0f; // disable Doppler for this test
        e.attenuationModel = AttenuationModel::Linear;
        mixer.setEmitter(v, lid, e);
        dev->clearCaptured();
        mixer.mix();
        const float p = peakAbs(dev->capturedBuffers().back());
        mixer.stop(v);
        return p;
    };

    // At minDistance — full gain. Emitter directly in front (axis-aligned)
    // so the equal-power pan gives ~0.707 per channel; peak then ≈ 0.5 *
    // 0.707 = 0.353. Verify it's in that band.
    const float pAtMin = playAt({ 0.0f, 0.0f, 1.0f }, 1.0f, 11.0f);
    CHECK(pAtMin > 0.3f);
    CHECK(pAtMin < 0.4f);

    // At midpoint — Linear curve says gain = 0.5 at d = 6.
    const float pAtMid = playAt({ 0.0f, 0.0f, 6.0f }, 1.0f, 11.0f);
    CHECK(pAtMid > 0.5f * pAtMin * 0.8f);   // within 20% of half
    CHECK(pAtMid < 0.5f * pAtMin * 1.2f);

    // At maxDistance — silence.
    const float pAtMax = playAt({ 0.0f, 0.0f, 11.0f }, 1.0f, 11.0f);
    CHECK_EQ(pAtMax, 0.0f);

    // Far beyond maxDistance — silence.
    const float pBeyond = playAt({ 0.0f, 0.0f, 100.0f }, 1.0f, 11.0f);
    CHECK_EQ(pBeyond, 0.0f);

    EXIT_WITH_RESULT();
}
