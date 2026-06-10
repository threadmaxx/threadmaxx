// AU2 — soloing bus A silences bus B; un-soloing returns both to audible.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

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

    std::vector<float> dc(256 * 2, 0.5f);
    SoundId clip = mixer.addClip(dc, AudioFormat{48000, 2, ChannelLayout::Stereo});

    BusId busA = mixer.createBus({});
    BusId busB = mixer.createBus({});

    VoiceId vA = mixer.play(VoiceDesc{ .sound = clip, .bus = busA, .looping = true });
    VoiceId vB = mixer.play(VoiceDesc{ .sound = clip, .bus = busB, .looping = true });
    CHECK(vA.value != 0);
    CHECK(vB.value != 0);

    // No solo: both audible.
    mixer.mix();
    CHECK(peakAbs(dev->capturedBuffers().back()) > 0.4f);

    // Solo A: B silenced, A heard. Peak ~= 0.5 (just A).
    mixer.setBusSolo(busA, true);
    dev->clearCaptured();
    mixer.mix();
    const float soloed = peakAbs(dev->capturedBuffers().back());
    CHECK(soloed > 0.4f);
    CHECK(soloed < 0.7f); // not both (which would peak ~1.0)

    // Solo both A and B: both audible (both are solo'd).
    mixer.setBusSolo(busB, true);
    dev->clearCaptured();
    mixer.mix();
    CHECK(peakAbs(dev->capturedBuffers().back()) > 0.9f);

    // Un-solo both: both audible, no solo logic.
    mixer.setBusSolo(busA, false);
    mixer.setBusSolo(busB, false);
    dev->clearCaptured();
    mixer.mix();
    CHECK(peakAbs(dev->capturedBuffers().back()) > 0.9f);

    EXIT_WITH_RESULT();
}
