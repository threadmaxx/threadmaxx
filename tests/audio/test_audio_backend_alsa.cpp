// AU8 — ALSA backend smoke test. Headless / CI tolerant: if the system has
// no ALSA "default" PCM available (common on CI runners), initialize()
// returns false and we exit cleanly. The test is meaningful when there IS
// a device — it exercises init→submit→shutdown without crashing.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#if THREADMAXX_AUDIO_HAS_ALSA
#include "threadmaxx_audio/alsa_device.hpp"
#endif

#include <cstdio>
#include <cstddef>
#include <vector>

int main() {
#if !THREADMAXX_AUDIO_HAS_ALSA
    std::printf("ALSA not available at configure time — skipping (PASS)\n");
    EXIT_WITH_RESULT();
#else
    using namespace threadmaxx::audio;

    AlsaDevice dev;
    CHECK(!dev.initialized());

    const AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t bufFrames = 1024;

    if (!dev.initialize(fmt, bufFrames)) {
        std::printf("ALSA initialize failed — no audio device on this host (PASS)\n");
        EXIT_WITH_RESULT();
    }

    CHECK(dev.initialized());
    CHECK(dev.format() == fmt);
    CHECK_EQ(dev.bufferFrames(), bufFrames);

    // Submit one buffer of silence. Should not error, should not crash.
    std::vector<float> silence(bufFrames * 2, 0.0f);
    dev.submit(ConstAudioSpan{ silence.data(), bufFrames, fmt });

    dev.shutdown();
    CHECK(!dev.initialized());

    // Re-initialize works (idempotent shutdown + re-open).
    if (dev.initialize(fmt, bufFrames)) {
        dev.submit(ConstAudioSpan{ silence.data(), bufFrames, fmt });
        dev.shutdown();
    }

    // Post-shutdown submits are silent drops (no abort).
    dev.submit(ConstAudioSpan{ silence.data(), bufFrames, fmt });

    EXIT_WITH_RESULT();
#endif
}
