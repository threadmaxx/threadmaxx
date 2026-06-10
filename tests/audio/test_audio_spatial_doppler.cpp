// AU4 — relative velocity along the listener-emitter axis shifts pitch by
// the expected factor. We sidestep an FFT by counting zero-crossings: a
// pitch increase yields proportionally more crossings per fixed-length
// output buffer.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

// Count sign changes in the left channel of an interleaved stereo capture.
static std::size_t countZeroCrossingsL(const std::vector<float>& buf) {
    std::size_t count = 0;
    if (buf.size() < 4) return 0;
    float prev = buf[0];
    for (std::size_t i = 2; i + 1 < buf.size(); i += 2) {
        const float cur = buf[i];
        if ((prev >= 0.0f && cur < 0.0f) || (prev < 0.0f && cur >= 0.0f)) ++count;
        prev = cur;
    }
    return count;
}

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner   = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();
    AudioMixer mixer(std::move(deviceOwner));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 1024;
    CHECK(mixer.initialize(fmt, bufFrames));

    // 1-second stereo sine @ 440 Hz, amplitude 0.5.
    constexpr std::size_t kSampleRate = 48000;
    constexpr float kFreq = 440.0f;
    constexpr float kPi   = 3.14159265358979323846f;
    std::vector<float> sine(kSampleRate * 2, 0.0f);
    for (std::size_t f = 0; f < kSampleRate; ++f) {
        const float t = static_cast<float>(f) / static_cast<float>(kSampleRate);
        const float v = 0.5f * std::sin(2.0f * kPi * kFreq * t);
        sine[f * 2]     = v;
        sine[f * 2 + 1] = v;
    }
    SoundId clip = mixer.addClip(sine, fmt);

    ListenerDesc l{};
    l.position = { 0.0f, 0.0f, 0.0f };
    l.forward  = { 0.0f, 0.0f, 1.0f };
    l.up       = { 0.0f, 1.0f, 0.0f };
    ListenerId lid = mixer.createListener(l);

    constexpr std::size_t kMixCalls = 8;

    auto runWithListenerVelocity = [&](Vec3 listenerVelocity) -> std::size_t {
        // Reset listener pose + velocity for this run.
        l.velocity = listenerVelocity;
        mixer.setListener(lid, l);

        VoiceId v = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
        EmitterDesc e{};
        e.position      = { 0.0f, 0.0f, 10.0f };
        e.velocity      = { 0.0f, 0.0f, 0.0f };
        e.minDistance   = 1.0f;
        e.maxDistance   = 1000.0f;
        e.dopplerFactor = 1.0f;
        mixer.setEmitter(v, lid, e);

        dev->clearCaptured();
        for (std::size_t i = 0; i < kMixCalls; ++i) mixer.mix();
        mixer.stop(v);

        std::size_t total = 0;
        for (const auto& buf : dev->capturedBuffers()) total += countZeroCrossingsL(buf);
        return total;
    };

    // Stationary listener: no Doppler. Expected zero crossings ≈
    // kMixCalls * bufFrames * 2 * freq / sampleRate.
    const std::size_t zStationary = runWithListenerVelocity({ 0.0f, 0.0f, 0.0f });
    const double expectedStationary =
        static_cast<double>(kMixCalls * bufFrames) * 2.0 * kFreq / static_cast<double>(kSampleRate);
    CHECK(std::abs(static_cast<long long>(zStationary) - static_cast<long long>(expectedStationary)) < 10);

    // Listener moving towards emitter (+Z) at 34.3 m/s ≈ c/10 → pitch shifts
    // up ~10%. Expect ~10% more zero crossings.
    const std::size_t zMovingIn = runWithListenerVelocity({ 0.0f, 0.0f, 34.3f });
    CHECK(zMovingIn > zStationary);
    const double ratioIn = static_cast<double>(zMovingIn) / static_cast<double>(zStationary);
    CHECK(ratioIn > 1.05);
    CHECK(ratioIn < 1.15);

    // Listener moving away (-Z) at 34.3 m/s → pitch shifts down ~10%.
    const std::size_t zMovingOut = runWithListenerVelocity({ 0.0f, 0.0f, -34.3f });
    CHECK(zMovingOut < zStationary);
    const double ratioOut = static_cast<double>(zMovingOut) / static_cast<double>(zStationary);
    CHECK(ratioOut > 0.85);
    CHECK(ratioOut < 0.95);

    EXIT_WITH_RESULT();
}
