// AU5 — fadeIn / fadeOut over N seconds produce monotonic gain envelopes;
// fadeIn samples sweep 0 → 1 across the fade region and stay at 1 after;
// fadeOut samples sweep 1 → 0 and stay at 0.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    constexpr float kSampleRate = 48000.0f;
    constexpr float kFadeSec    = 0.01f;                       // 10 ms
    const std::size_t fadeFr    = static_cast<std::size_t>(kFadeSec * kSampleRate); // 480
    const std::size_t totalFr   = fadeFr + 128;
    AudioFormat fmt{48000, 2, ChannelLayout::Stereo};

    // ---------- fadeIn ----------
    {
        std::vector<float> buf(totalFr * 2, 1.0f); // DC source = 1.0
        applyFadeIn(AudioSpan{ buf.data(), totalFr, fmt }, kFadeSec, kSampleRate);

        // sample[0] silenced.
        CHECK_EQ(buf[0], 0.0f);
        CHECK_EQ(buf[1], 0.0f);

        // sample[fadeFr - 1] reaches full gain (1.0).
        const std::size_t lastFade = fadeFr - 1;
        CHECK(std::fabs(buf[lastFade * 2]     - 1.0f) < 1e-5f);
        CHECK(std::fabs(buf[lastFade * 2 + 1] - 1.0f) < 1e-5f);

        // Beyond the fade region: full gain.
        for (std::size_t f = fadeFr; f < totalFr; ++f) {
            CHECK(std::fabs(buf[f * 2]     - 1.0f) < 1e-5f);
            CHECK(std::fabs(buf[f * 2 + 1] - 1.0f) < 1e-5f);
        }

        // Monotonic non-decreasing through the fade region.
        for (std::size_t f = 1; f < fadeFr; ++f) {
            CHECK(buf[f * 2] >= buf[(f - 1) * 2] - 1e-6f);
        }
    }

    // ---------- fadeOut ----------
    {
        std::vector<float> buf(totalFr * 2, 1.0f);
        applyFadeOut(AudioSpan{ buf.data(), totalFr, fmt }, kFadeSec, kSampleRate);

        // sample[0] passes through unchanged.
        CHECK(std::fabs(buf[0] - 1.0f) < 1e-5f);
        CHECK(std::fabs(buf[1] - 1.0f) < 1e-5f);

        // sample[fadeFr - 1] reaches zero.
        const std::size_t lastFade = fadeFr - 1;
        CHECK_EQ(buf[lastFade * 2],     0.0f);
        CHECK_EQ(buf[lastFade * 2 + 1], 0.0f);

        // After the fade: silence.
        for (std::size_t f = fadeFr; f < totalFr; ++f) {
            CHECK_EQ(buf[f * 2],     0.0f);
            CHECK_EQ(buf[f * 2 + 1], 0.0f);
        }

        // Monotonic non-increasing through the fade region.
        for (std::size_t f = 1; f < fadeFr; ++f) {
            CHECK(buf[f * 2] <= buf[(f - 1) * 2] + 1e-6f);
        }
    }

    // ---------- Edge cases ----------
    {
        std::vector<float> buf(32 * 2, 0.5f);
        // Zero seconds → no-op.
        applyFadeIn(AudioSpan{ buf.data(), 32, fmt }, 0.0f, kSampleRate);
        for (float v : buf) CHECK_EQ(v, 0.5f);

        // Negative sample rate → no-op.
        applyFadeOut(AudioSpan{ buf.data(), 32, fmt }, kFadeSec, -1.0f);
        for (float v : buf) CHECK_EQ(v, 0.5f);
    }

    EXIT_WITH_RESULT();
}
