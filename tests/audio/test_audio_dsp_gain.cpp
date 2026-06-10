// AU5 — applyGain at 0 dB is a no-op (bit-exact); -∞ dB silences every
// sample.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <limits>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    std::vector<float> buf(512 * 2, 0.0f);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<float>(i) * 0.001f - 0.25f;
    }
    const std::vector<float> original = buf;

    // 0 dB is bit-exact no-op (× 1.0 in IEEE-754).
    applyGain(AudioSpan{ buf.data(), 512, fmt }, 0.0f);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        CHECK_EQ(buf[i], original[i]);
    }

    // -∞ dB → silence everywhere.
    applyGain(AudioSpan{ buf.data(), 512, fmt }, -std::numeric_limits<float>::infinity());
    for (float v : buf) {
        CHECK_EQ(v, 0.0f);
    }

    // -6 dB halves amplitude (10^(-6/20) ≈ 0.501).
    buf = original;
    applyGain(AudioSpan{ buf.data(), 512, fmt }, -6.0f);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const float expected = original[i] * 0.5011872336f;
        const float diff     = buf[i] - expected;
        CHECK(diff > -1e-4f && diff < 1e-4f);
    }

    EXIT_WITH_RESULT();
}
