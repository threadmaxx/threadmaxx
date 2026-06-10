// AU3 — a looping voice on a finite stream wraps cleanly at end-of-stream
// without dropping samples; rewind happens inside the mixer's read path.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <memory>

int main() {
    using namespace threadmaxx::audio;

    auto deviceOwner   = std::make_unique<LoopbackDevice>();
    LoopbackDevice* dev = deviceOwner.get();
    AudioMixer mixer(std::move(deviceOwner));
    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 256;
    CHECK(mixer.initialize(fmt, bufFrames));

    // Finite 384-frame stream. With bufFrames=256, the second mix() call
    // hits EOF mid-buffer (128 frames in) and must rewind to fill the rest.
    auto streamOwner  = std::make_unique<NoiseStream>(fmt, 384u);
    NoiseStream* nptr = streamOwner.get();
    StreamId sid      = mixer.addStream(std::move(streamOwner));

    VoiceId v = mixer.play(VoiceDesc{ .stream = sid, .looping = true });
    CHECK(v.value != 0);

    // First mix consumes 256 frames of the 384-frame stream.
    mixer.mix();
    CHECK_EQ(nptr->cursor(), std::size_t{256});
    CHECK(!nptr->finished());

    // Second mix: reads remaining 128 frames, then stream.finished(), but
    // voice is looping → mixer rewinds and reads 128 more from the start.
    // Final cursor should be 128 (after rewind).
    mixer.mix();
    CHECK_EQ(nptr->cursor(), std::size_t{128});
    CHECK(!nptr->finished()); // because we rewound

    // No underruns from a healthy finite stream + looping voice.
    CHECK_EQ(mixer.stats().underruns, std::uint32_t{0});

    // Voice still playing.
    CHECK(mixer.isPlaying(v));

    // The captured second buffer must have no zero-filled tail (every
    // frame got real samples from the stream).
    const auto& buf = dev->capturedBuffers().back();
    bool foundNonzeroInTail = false;
    const std::size_t tailStart = (bufFrames / 2u) * fmt.channels;
    for (std::size_t i = tailStart; i < buf.size(); ++i) {
        if (buf[i] != 0.0f) { foundNonzeroInTail = true; break; }
    }
    CHECK(foundNonzeroInTail);

    EXIT_WITH_RESULT();
}
