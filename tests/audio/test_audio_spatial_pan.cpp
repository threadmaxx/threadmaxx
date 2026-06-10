// AU4 — emitter to the listener's left produces a left-heavy mix; directly
// behind produces equal-power pan with an audible "behind" attenuation.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

// Per-channel peak across an interleaved stereo buffer.
static void perChannelPeak(const std::vector<float>& buf, float& peakL, float& peakR) {
    peakL = peakR = 0.0f;
    for (std::size_t i = 0; i + 1 < buf.size(); i += 2) {
        const float al = std::fabs(buf[i]);
        const float ar = std::fabs(buf[i + 1]);
        if (al > peakL) peakL = al;
        if (ar > peakR) peakR = ar;
    }
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

    // Listener at origin facing +Z, up +Y → right = +X (right-handed).
    ListenerDesc l{};
    l.position = { 0.0f, 0.0f, 0.0f };
    l.forward  = { 0.0f, 0.0f, 1.0f };
    l.up       = { 0.0f, 1.0f, 0.0f };
    ListenerId lid = mixer.createListener(l);

    auto playFrom = [&](Vec3 emitterPos) -> std::pair<float, float> {
        VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
        EmitterDesc e{};
        e.position      = emitterPos;
        e.minDistance   = 1.0f;
        e.maxDistance   = 100.0f;
        e.dopplerFactor = 0.0f;
        mixer.setEmitter(v, lid, e);
        dev->clearCaptured();
        mixer.mix();
        float pL = 0.0f, pR = 0.0f;
        perChannelPeak(dev->capturedBuffers().back(), pL, pR);
        mixer.stop(v);
        return { pL, pR };
    };

    // Emitter directly to listener's LEFT (-X side): panX = -1 → full left.
    const auto [pLleft, pRleft] = playFrom({ -1.0f, 0.0f, 0.0f });
    CHECK(pLleft > 0.4f);
    CHECK(pRleft < 0.02f);
    CHECK(pLleft > pRleft * 10.0f);

    // Emitter directly to RIGHT (+X): panX = +1 → full right.
    const auto [pLright, pRright] = playFrom({ 1.0f, 0.0f, 0.0f });
    CHECK(pRright > 0.4f);
    CHECK(pLright < 0.02f);
    CHECK(pRright > pLright * 10.0f);

    // Emitter directly IN FRONT (+Z, axis-aligned with forward): equal-power
    // pan, no behind-atten.
    const auto [pLfront, pRfront] = playFrom({ 0.0f, 0.0f, 1.0f });
    CHECK(std::fabs(pLfront - pRfront) < 1e-4f);
    CHECK(pLfront > 0.3f);

    // Emitter directly BEHIND (-Z): equal-power pan but with the documented
    // ~0.7x behind attenuation. Peak should be lower than the in-front peak
    // (and clearly above zero).
    const auto [pLback, pRback] = playFrom({ 0.0f, 0.0f, -1.0f });
    CHECK(std::fabs(pLback - pRback) < 1e-4f);
    CHECK(pLback > 0.15f);
    CHECK(pLback < pLfront);

    EXIT_WITH_RESULT();
}
