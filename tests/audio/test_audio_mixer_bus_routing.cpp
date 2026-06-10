// AU2 — voice routed to a muted bus produces silence; un-mute resumes audible
// output.

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
    CHECK(mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 256));

    // Constant DC clip = always-on signal — easy peak check.
    std::vector<float> dc(256 * 2, 0.5f);
    SoundId clip = mixer.addClip(dc, AudioFormat{48000, 2, ChannelLayout::Stereo});

    BusId routing = mixer.createBus(BusDesc{});
    CHECK(routing.value != 0);
    CHECK(routing != mixer.masterBus());
    CHECK(mixer.isValidBus(routing));

    VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .bus = routing, .looping = true });
    CHECK(v.value != 0);

    // Un-muted bus: audible.
    mixer.mix();
    CHECK(peakAbs(dev->capturedBuffers().back()) > 0.4f);

    // Mute the bus: silence.
    mixer.setBusMute(routing, true);
    dev->clearCaptured();
    mixer.mix();
    CHECK_EQ(peakAbs(dev->capturedBuffers().back()), 0.0f);

    // Un-mute: audible again.
    mixer.setBusMute(routing, false);
    dev->clearCaptured();
    mixer.mix();
    CHECK(peakAbs(dev->capturedBuffers().back()) > 0.4f);

    // Bus gain attenuation: -20dB cuts amplitude by ~10x.
    mixer.setBusGain(routing, -20.0f);
    dev->clearCaptured();
    mixer.mix();
    const float attenuated = peakAbs(dev->capturedBuffers().back());
    CHECK(attenuated > 0.01f);
    CHECK(attenuated < 0.1f);

    EXIT_WITH_RESULT();
}
