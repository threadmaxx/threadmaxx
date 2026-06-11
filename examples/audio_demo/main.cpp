// threadmaxx_audio v1.0 — minimal end-to-end integration example.
//
// Pattern (matches AU7's recommended ISystem shape):
//   1. Set up an `AudioMixer` with a `LoopbackDevice` (or AU8 real backend).
//   2. Register a listener; load any clips you need up front.
//   3. Per tick (game loop):
//        a. Update listener pose from your camera / player Transform.
//        b. For each tracked emitter entity: call `setEmitterPose` with the
//           latest world Transform + Velocity.
//        c. Call `mixer.mix()` to produce one audio buffer and submit it.
//
// This driver runs the loop headlessly (LoopbackDevice → capture buffer)
// for a fixed number of buffers, printing the mixer's peak meter so you
// can see audio actually flowed.

#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    // 1. Mixer + device.
    auto device = std::make_unique<LoopbackDevice>();
    AudioMixer mixer(std::move(device));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    if (!mixer.initialize(fmt, 1024)) {
        std::fprintf(stderr, "mixer.initialize failed\n");
        return 1;
    }

    // 2. Listener at origin facing +Z, and a clip + voice + emitter
    //    attached in front of the listener.
    ListenerId listenerId = mixer.createListener({});
    {
        ListenerDesc l{};
        l.position = { 0.0f, 0.0f, 0.0f };
        l.forward  = { 0.0f, 0.0f, 1.0f };
        l.up       = { 0.0f, 1.0f, 0.0f };
        setListenerPose(mixer, listenerId,
                        l.position, l.velocity, l.forward, l.up);
    }

    constexpr float kFreq = 440.0f;
    constexpr std::size_t kClipFrames = 48000; // 1 second
    std::vector<float> sine(kClipFrames * 2, 0.0f);
    for (std::size_t f = 0; f < kClipFrames; ++f) {
        const float t = static_cast<float>(f) / static_cast<float>(fmt.sampleRate);
        const float v = 0.5f * std::sin(2.0f * 3.14159265358979f * kFreq * t);
        sine[f * 2]     = v;
        sine[f * 2 + 1] = v;
    }
    SoundId clip = mixer.addClip(sine, fmt);
    VoiceId voice = mixer.play(VoiceDesc{ .sound = clip, .looping = true });

    // 3. Loop. Simulate the emitter moving away from the listener over
    //    time so the captured peak decays — that's the visible signal
    //    that the per-tick position updates flowed through.
    constexpr int kTicks = 100;
    for (int t = 0; t < kTicks; ++t) {
        const float z = 1.0f + static_cast<float>(t) * 0.4f; // grows past maxDistance
        setEmitterPose(mixer, voice, listenerId,
                       /*pos*/ { 0.0f, 0.0f, z },
                       /*vel*/ { 0.0f, 0.0f, 0.0f },
                       /*minDistance*/ 1.0f,
                       /*maxDistance*/ 30.0f,
                       /*dopplerFactor*/ 0.0f,
                       AttenuationModel::Linear);
        mixer.mix();
    }

    const MixerStats s = mixer.stats();
    std::printf("audio_demo: ran %d ticks (1024 frames each)\n", kTicks);
    std::printf("  activeVoices=%u  droppedVoices=%u  underruns=%u\n",
                s.activeVoices, s.droppedVoices, s.underruns);
    std::printf("  hold-max peakL=%.4f peakR=%.4f  last rmsL=%.4f rmsR=%.4f\n",
                s.peakL, s.peakR, s.rmsL, s.rmsR);
    return 0;
}
