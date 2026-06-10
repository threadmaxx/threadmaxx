// AU2 — exhausting the voice pool with MaxVoices=8 plus a 9th play() steals
// the oldest; MixerStats::droppedVoices increments and the old VoiceId
// decodes as stale.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <memory>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    AudioMixerConfig cfg{};
    cfg.maxVoices = 8;
    AudioMixer mixer(std::make_unique<LoopbackDevice>(), cfg);
    CHECK(mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 256));

    std::vector<float> dc(256 * 2, 0.5f);
    SoundId clip = mixer.addClip(dc, AudioFormat{48000, 2, ChannelLayout::Stereo});

    // Fill the pool with 8 looping voices.
    std::vector<VoiceId> voices;
    for (int i = 0; i < 8; ++i) {
        voices.push_back(mixer.play(VoiceDesc{ .sound = clip, .looping = true }));
        CHECK(voices.back().value != 0);
    }
    CHECK_EQ(mixer.stats().activeVoices, std::uint32_t{8});
    CHECK_EQ(mixer.stats().droppedVoices, std::uint32_t{0});

    // The 9th play steals the oldest (voices[0]) — droppedVoices increments.
    VoiceId ninth = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
    CHECK(ninth.value != 0);
    CHECK_EQ(mixer.stats().activeVoices, std::uint32_t{8});
    CHECK_EQ(mixer.stats().droppedVoices, std::uint32_t{1});

    // The stolen voice's VoiceId is now stale; the rest are still alive.
    CHECK(!mixer.isPlaying(voices[0]));
    for (std::size_t i = 1; i < voices.size(); ++i) {
        CHECK(mixer.isPlaying(voices[i]));
    }
    CHECK(mixer.isPlaying(ninth));

    // A 10th play steals the next oldest (voices[1]).
    VoiceId tenth = mixer.play(VoiceDesc{ .sound = clip, .looping = true });
    CHECK_EQ(mixer.stats().droppedVoices, std::uint32_t{2});
    CHECK(!mixer.isPlaying(voices[1]));
    CHECK(mixer.isPlaying(tenth));

    // Stopping a voice frees its slot; the next play() does NOT steal.
    mixer.stop(voices[2]);
    CHECK(!mixer.isPlaying(voices[2]));
    CHECK_EQ(mixer.stats().activeVoices, std::uint32_t{7});

    VoiceId fresh = mixer.play(VoiceDesc{ .sound = clip });
    CHECK(fresh.value != 0);
    CHECK_EQ(mixer.stats().activeVoices, std::uint32_t{8});
    CHECK_EQ(mixer.stats().droppedVoices, std::uint32_t{2}); // unchanged

    EXIT_WITH_RESULT();
}
