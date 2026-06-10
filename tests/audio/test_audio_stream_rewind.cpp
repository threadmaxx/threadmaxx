// AU3 — `rewind()` resets the read cursor and `finished()` is false again.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <memory>

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner = std::make_unique<LoopbackDevice>();
    AudioMixer mixer(std::move(deviceOwner));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 512;
    CHECK(mixer.initialize(fmt, bufFrames));

    // Finite 1024-frame stream — finishes after exactly two 512-frame mix
    // calls.
    auto streamOwner   = std::make_unique<NoiseStream>(fmt, 1024u);
    NoiseStream* nptr  = streamOwner.get();
    StreamId sid       = mixer.addStream(std::move(streamOwner));
    CHECK(nptr->totalFrames() == std::size_t{1024});
    CHECK(!nptr->finished());

    // Play non-looping.
    VoiceId v = mixer.play(VoiceDesc{ .stream = sid });
    CHECK(v.value != 0);

    // Two mixes consume the stream.
    mixer.mix();
    CHECK(!nptr->finished());
    CHECK_EQ(nptr->cursor(), std::size_t{512});

    mixer.mix();
    CHECK(nptr->finished());
    CHECK_EQ(nptr->cursor(), std::size_t{1024});

    // Voice auto-stops on the next mix call when the non-looping stream is
    // empty (read returns 0, stream.finished() is true).
    mixer.mix();
    CHECK(!mixer.isPlaying(v));

    // rewind() resets cursor and clears finished.
    nptr->rewind();
    CHECK(!nptr->finished());
    CHECK_EQ(nptr->cursor(), std::size_t{0});

    // The mixer can use the rewound stream for a fresh voice.
    VoiceId v2 = mixer.play(VoiceDesc{ .stream = sid });
    CHECK(v2.value != 0);
    mixer.mix();
    CHECK_EQ(nptr->cursor(), std::size_t{512});

    EXIT_WITH_RESULT();
}
