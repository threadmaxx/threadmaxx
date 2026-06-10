// AU1 — LoopbackDevice initialize → submit 1024 frames of stereo silence →
// shutdown. Captured buffer length matches; format round-trips; post-shutdown
// submits are silently dropped.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    LoopbackDevice dev;
    CHECK(!dev.initialized());
    CHECK_EQ(dev.bufferFrames(), std::size_t{0});

    AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    CHECK(dev.initialize(fmt, 1024));
    CHECK(dev.initialized());
    CHECK(dev.format() == fmt);
    CHECK_EQ(dev.bufferFrames(), std::size_t{1024});
    CHECK_EQ(dev.capturedBuffers().size(), std::size_t{0});

    // Submit 1024 frames of stereo silence.
    std::vector<float> silence(samplesIn(fmt, 1024), 0.0f);
    ConstAudioSpan submission{silence.data(), 1024, fmt};
    dev.submit(submission);
    CHECK_EQ(dev.capturedBuffers().size(), std::size_t{1});
    CHECK_EQ(dev.capturedBuffers().front().size(), samplesIn(fmt, 1024));
    CHECK_EQ(dev.totalFramesCaptured(), std::size_t{1024});

    // The captured payload bytes match the submission.
    for (float v : dev.capturedBuffers().front()) {
        CHECK(v == 0.0f);
    }

    // Submit a second frame with a sentinel pattern. Layout must round-trip
    // into the captured buffer in order.
    std::vector<float> pattern(samplesIn(fmt, 1024), 0.0f);
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<float>(i) * 0.001f;
    }
    dev.submit(ConstAudioSpan{pattern.data(), 1024, fmt});
    CHECK_EQ(dev.capturedBuffers().size(), std::size_t{2});
    CHECK(dev.capturedBuffers().back() == pattern);
    CHECK_EQ(dev.totalFramesCaptured(), std::size_t{2048});

    // clearCaptured wipes the history but keeps the device initialized.
    dev.clearCaptured();
    CHECK_EQ(dev.capturedBuffers().size(), std::size_t{0});
    CHECK(dev.initialized());

    // Shutdown disables further capture; the contract is "silent drop".
    dev.shutdown();
    CHECK(!dev.initialized());
    CHECK_EQ(dev.bufferFrames(), std::size_t{0});
    dev.submit(ConstAudioSpan{silence.data(), 1024, fmt});
    CHECK_EQ(dev.capturedBuffers().size(), std::size_t{0});

    // Re-initialize succeeds.
    AudioFormat mono{44100, 1, ChannelLayout::Mono};
    CHECK(dev.initialize(mono, 512));
    CHECK(dev.format() == mono);
    CHECK_EQ(dev.bufferFrames(), std::size_t{512});

    // initialize() rejects zero buffer frames and zero channels.
    LoopbackDevice bad1;
    CHECK(!bad1.initialize(fmt, 0));
    CHECK(!bad1.initialized());
    LoopbackDevice bad2;
    AudioFormat zero{48000, 0, ChannelLayout::Stereo};
    CHECK(!bad2.initialize(zero, 1024));
    CHECK(!bad2.initialized());

    EXIT_WITH_RESULT();
}
