// AU9 — `AudioMixer::listBuses` / `AudioDiagnostics::listBuses` enumerate
// every live bus (including the master), surface per-bus mute / solo / gain
// state, and tally live voices routing into each slot.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <memory>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner = std::make_unique<LoopbackDevice>();
    AudioMixer mixer(std::move(deviceOwner));
    CHECK(mixer.initialize(AudioFormat{48000, 2, ChannelLayout::Stereo}, 256));

    // Initial state: only the master bus is live.
    {
        const auto buses = mixer.listBuses();
        CHECK_EQ(buses.size(), 1u);
        CHECK(buses[0].isMaster);
        CHECK(buses[0].id == mixer.masterBus());
        CHECK_EQ(buses[0].voiceCount, 0u);
        CHECK_EQ(buses[0].muted, false);
        CHECK_EQ(buses[0].solo, false);
    }

    // Add two routing buses with custom gain / mute. Master + 2.
    BusDesc d1{}; d1.gainDb = -6.0f;
    BusId b1 = mixer.createBus(d1);
    CHECK(b1.value != 0);
    BusDesc d2{}; d2.muted = true;
    BusId b2 = mixer.createBus(d2);
    CHECK(b2.value != 0);

    // Wire a couple of voices to inspect voiceCount per bus.
    std::vector<float> dc(256 * 2, 0.5f);
    SoundId clip = mixer.addClip(dc, AudioFormat{48000, 2, ChannelLayout::Stereo});
    VoiceId v1 = mixer.play(VoiceDesc{ .sound = clip, .bus = b1, .looping = true });
    VoiceId v2 = mixer.play(VoiceDesc{ .sound = clip, .bus = b1, .looping = true });
    VoiceId v3 = mixer.play(VoiceDesc{ .sound = clip, .bus = b2, .looping = true });
    (void)v1; (void)v2; (void)v3;

    {
        const auto buses = mixer.listBuses();
        CHECK_EQ(buses.size(), 3u);

        bool sawMaster = false, sawB1 = false, sawB2 = false;
        for (const auto& s : buses) {
            if (s.isMaster) {
                sawMaster = true;
                CHECK(s.id == mixer.masterBus());
                CHECK_EQ(s.voiceCount, 0u);
            } else if (s.id == b1) {
                sawB1 = true;
                CHECK(!s.isMaster);
                CHECK_EQ(s.gainDb, -6.0f);
                CHECK(!s.muted);
                CHECK_EQ(s.voiceCount, 2u);
            } else if (s.id == b2) {
                sawB2 = true;
                CHECK(s.muted);
                CHECK_EQ(s.voiceCount, 1u);
            }
        }
        CHECK(sawMaster);
        CHECK(sawB1);
        CHECK(sawB2);
    }

    // The diagnostics view returns the same data via pass-through.
    AudioDiagnostics diag{mixer};
    const auto viaDiag = diag.listBuses();
    CHECK_EQ(viaDiag.size(), 3u);

    // Destroying a bus drops it from the list.
    mixer.destroyBus(b2);
    {
        const auto buses = mixer.listBuses();
        CHECK_EQ(buses.size(), 2u);
        for (const auto& s : buses) {
            CHECK(!(s.id == b2));
        }
    }

    return gTestFailures == 0 ? 0 : 1;
}
