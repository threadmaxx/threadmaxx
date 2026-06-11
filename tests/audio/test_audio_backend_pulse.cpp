// AU8 — PulseAudio backend smoke test. Same tolerant shape as the ALSA
// test: no pulse daemon → initialize() returns false → exit cleanly.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#if THREADMAXX_AUDIO_HAS_PULSE
#include "threadmaxx_audio/pulse_device.hpp"
#endif

#include <cstdio>
#include <cstddef>
#include <vector>

int main() {
#if !THREADMAXX_AUDIO_HAS_PULSE
    std::printf("PulseAudio not available at configure time — skipping (PASS)\n");
    EXIT_WITH_RESULT();
#else
    using namespace threadmaxx::audio;

    PulseDevice dev;
    CHECK(!dev.initialized());

    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 1024;

    if (!dev.initialize(fmt, bufFrames)) {
        std::printf("Pulse initialize failed — no pulse daemon on this host (PASS)\n");
        EXIT_WITH_RESULT();
    }

    CHECK(dev.initialized());
    CHECK(dev.format() == fmt);
    CHECK_EQ(dev.bufferFrames(), bufFrames);

    std::vector<float> silence(bufFrames * 2, 0.0f);
    dev.submit(ConstAudioSpan{ silence.data(), bufFrames, fmt });

    dev.shutdown();
    CHECK(!dev.initialized());

    if (dev.initialize(fmt, bufFrames)) {
        dev.submit(ConstAudioSpan{ silence.data(), bufFrames, fmt });
        dev.shutdown();
    }

    dev.submit(ConstAudioSpan{ silence.data(), bufFrames, fmt });

    EXIT_WITH_RESULT();
#endif
}
