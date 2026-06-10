// AU2 — one-shot clip playback: register a 1-second sine, play() returns a
// VoiceId, mix N frames, loopback captures audible output above silence.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner   = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();

    AudioMixer mixer(std::move(deviceOwner), AudioMixerConfig{});
    CHECK(mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 1024));
    CHECK(mixer.initialized());

    // 1-second stereo sine @ 440 Hz, amplitude 0.5.
    constexpr std::size_t kSampleRate = 48000;
    std::vector<float> sine(kSampleRate * 2, 0.0f);
    constexpr float kPi = 3.14159265358979323846f;
    for (std::size_t f = 0; f < kSampleRate; ++f) {
        const float t = static_cast<float>(f) / static_cast<float>(kSampleRate);
        const float v = 0.5f * std::sin(2.0f * kPi * 440.0f * t);
        sine[f * 2]     = v;
        sine[f * 2 + 1] = v;
    }
    AudioFormat sineFmt{48000, 2, ChannelLayout::Stereo};
    SoundId clip = mixer.addClip(sine, sineFmt);
    CHECK(clip.value != 0);
    CHECK(mixer.isValidClip(clip));

    VoiceId voice = mixer.play(VoiceDesc{ .sound = clip });
    CHECK(voice.value != 0);
    CHECK(mixer.isPlaying(voice));

    // Mix a buffer and check captured output is above silence.
    mixer.mix();
    CHECK_EQ(dev->capturedBuffers().size(), std::size_t{1});
    const auto& buf = dev->capturedBuffers().front();
    CHECK_EQ(buf.size(), samplesIn(sineFmt, 1024));

    float peak = 0.0f;
    for (float v : buf) {
        const float a = v < 0.0f ? -v : v;
        if (a > peak) peak = a;
    }
    CHECK(peak > 0.1f);

    // Mixer stats reflect a single live voice.
    MixerStats stats = mixer.stats();
    CHECK_EQ(stats.activeVoices, std::uint32_t{1});
    CHECK_EQ(stats.droppedVoices, std::uint32_t{0});

    // Non-looping voice plays to completion: ~1 second @ 48kHz, 1024 frames per
    // mix call = 47 full buffers exhaust the clip. Iterate plenty.
    for (int i = 0; i < 64; ++i) mixer.mix();
    CHECK(!mixer.isPlaying(voice));
    CHECK_EQ(mixer.stats().activeVoices, std::uint32_t{0});

    EXIT_WITH_RESULT();
}
