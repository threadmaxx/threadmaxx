// AU4 — two listeners get independent attenuation/pan (split-screen use
// case). Each voice is `setEmitter`-attached to a different listener; the
// same physical emitter position resolves to different per-listener gains.

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

    std::vector<float> dc(bufFrames * 2, 0.5f);
    SoundId clip = mixer.addClip(dc, fmt);

    // Two listeners far apart.
    ListenerDesc l1{};
    l1.position = { 0.0f, 0.0f, 0.0f };
    l1.forward  = { 0.0f, 0.0f, 1.0f };
    l1.up       = { 0.0f, 1.0f, 0.0f };
    ListenerId lid1 = mixer.createListener(l1);

    ListenerDesc l2{};
    l2.position = { 100.0f, 0.0f, 0.0f };
    l2.forward  = { 0.0f, 0.0f, 1.0f };
    l2.up       = { 0.0f, 1.0f, 0.0f };
    ListenerId lid2 = mixer.createListener(l2);
    CHECK(lid1.value != 0);
    CHECK(lid2.value != 0);
    CHECK(lid1 != lid2);

    // One emitter near L1 (well inside its range), far from L2 (outside).
    const Vec3 emitterPos { 0.0f, 0.0f, 1.0f };
    EmitterDesc e{};
    e.position      = emitterPos;
    e.minDistance   = 1.0f;
    e.maxDistance   = 10.0f;
    e.dopplerFactor = 0.0f;

    // Voice A on L1, voice B on L2 (each its own play() call).
    VoiceId vA = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
    VoiceId vB = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
    mixer.setEmitter(vA, lid1, e);
    mixer.setEmitter(vB, lid2, e);

    mixer.mix();

    // Both voices contribute to the master. Stop B to inspect just A.
    mixer.stop(vB);
    dev->clearCaptured();
    mixer.mix();
    const float peakAonly = peakAbs(dev->capturedBuffers().back());

    // Restart B (on L2) and stop A: only B contributes — should be silent
    // since L2 is far beyond maxDistance from the emitter.
    mixer.stop(vA);
    VoiceId vB2 = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
    mixer.setEmitter(vB2, lid2, e);
    dev->clearCaptured();
    mixer.mix();
    const float peakBonly = peakAbs(dev->capturedBuffers().back());

    CHECK(peakAonly > 0.2f);          // L1 hears the emitter clearly
    CHECK_EQ(peakBonly, 0.0f);        // L2 is past maxDistance → silence

    // Move L2 close to the emitter: now B becomes audible too.
    l2.position = { 0.0f, 0.0f, 1.0f };
    mixer.setListener(lid2, l2);
    dev->clearCaptured();
    mixer.mix();
    const float peakBclose = peakAbs(dev->capturedBuffers().back());
    CHECK(peakBclose > 0.2f);

    EXIT_WITH_RESULT();
}
