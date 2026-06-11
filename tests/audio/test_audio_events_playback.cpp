// AU6 — playback callback fires VoiceStarted on play(), VoiceLooped when a
// looping clip wraps, VoiceStopped on explicit stop() and mix-time auto-stop.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace {
struct EventCollector {
    std::vector<threadmaxx::audio::PlaybackEvent> events;
};
}

static void onEvent(const threadmaxx::audio::PlaybackEvent& ev, void* user) {
    static_cast<EventCollector*>(user)->events.push_back(ev);
}

int main() {
    using namespace threadmaxx::audio;

    AudioMixer mixer(std::make_unique<LoopbackDevice>());
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 256;
    CHECK(mixer.initialize(fmt, bufFrames));

    EventCollector c;
    mixer.setPlaybackEventCallback(&onEvent, &c);

    // Short looping clip: 128 frames. With bufFrames=256, every mix call
    // wraps the clip → fires VoiceLooped.
    std::vector<float> shortClip(128 * 2, 0.5f);
    SoundId shortSound = mixer.addClip(shortClip, fmt);

    VoiceId vLoop = mixer.play(VoiceDesc{ .sound = shortSound, .looping = true });
    CHECK(vLoop.value != 0);

    // VoiceStarted observed immediately.
    CHECK_EQ(c.events.size(), std::size_t{1});
    CHECK(c.events[0].type  == PlaybackEventType::VoiceStarted);
    CHECK(c.events[0].voice == vLoop);
    CHECK(c.events[0].sound == shortSound);
    CHECK_EQ(c.events[0].stream.value, std::uint64_t{0});

    // One mix → VoiceLooped (clip wraps within the buffer).
    mixer.mix();
    CHECK_EQ(c.events.size(), std::size_t{2});
    CHECK(c.events[1].type  == PlaybackEventType::VoiceLooped);
    CHECK(c.events[1].voice == vLoop);

    // Explicit stop → VoiceStopped.
    mixer.stop(vLoop);
    CHECK_EQ(c.events.size(), std::size_t{3});
    CHECK(c.events[2].type == PlaybackEventType::VoiceStopped);
    CHECK(c.events[2].voice == vLoop);

    // Non-looping clip auto-stops at end; mix-time VoiceStopped fires.
    // 256-frame clip, bufFrames=256 → finishes in exactly one mix() call.
    std::vector<float> oneShot(256 * 2, 0.5f);
    SoundId oneShotId = mixer.addClip(oneShot, fmt);
    c.events.clear();

    VoiceId vOnce = mixer.play(VoiceDesc{ .sound = oneShotId, .looping = false });
    CHECK_EQ(c.events.size(), std::size_t{1});
    CHECK(c.events[0].type == PlaybackEventType::VoiceStarted);

    mixer.mix(); // consumes the entire 256-frame clip
    // After exactly one mix, the clip exhausted → VoiceStopped fires this
    // call (or the next, depending on the inclusive end check). Mix once
    // more to guarantee it surfaces.
    mixer.mix();

    bool sawStopped = false;
    for (const auto& ev : c.events) {
        if (ev.type == PlaybackEventType::VoiceStopped && ev.voice == vOnce) sawStopped = true;
    }
    CHECK(sawStopped);

    // Callback clear: no more events.
    mixer.setPlaybackEventCallback(nullptr, nullptr);
    c.events.clear();
    VoiceId vSilent = mixer.play(VoiceDesc{ .sound = shortSound });
    CHECK(vSilent.value != 0);
    CHECK_EQ(c.events.size(), std::size_t{0});

    EXIT_WITH_RESULT();
}
