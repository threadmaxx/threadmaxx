// AU5 — applyPanStereo at -1 / 0 / +1 produces left-only / center / right-
// only. Equal-power center keeps L = R = source × √2/2.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    AudioFormat fmt{48000, 2, ChannelLayout::Stereo};
    const std::size_t frames = 64;

    auto makeBuf = []() {
        std::vector<float> b(64 * 2, 0.5f);
        return b;
    };

    // pan = -1 → full left: L unchanged, R == 0.
    {
        auto buf = makeBuf();
        applyPanStereo(AudioSpan{ buf.data(), frames, fmt }, -1.0f);
        for (std::size_t f = 0; f < frames; ++f) {
            const float l = buf[f * 2];
            const float r = buf[f * 2 + 1];
            CHECK(std::fabs(l - 0.5f) < 1e-5f);
            CHECK(std::fabs(r) < 1e-5f);
        }
    }

    // pan = 0 → equal-power center: L == R == 0.5 × √2/2.
    {
        auto buf = makeBuf();
        applyPanStereo(AudioSpan{ buf.data(), frames, fmt }, 0.0f);
        const float expected = 0.5f * 0.7071067811865476f;
        for (std::size_t f = 0; f < frames; ++f) {
            const float l = buf[f * 2];
            const float r = buf[f * 2 + 1];
            CHECK(std::fabs(l - expected) < 1e-5f);
            CHECK(std::fabs(r - expected) < 1e-5f);
        }
    }

    // pan = +1 → full right: R unchanged, L == 0.
    {
        auto buf = makeBuf();
        applyPanStereo(AudioSpan{ buf.data(), frames, fmt }, 1.0f);
        for (std::size_t f = 0; f < frames; ++f) {
            const float l = buf[f * 2];
            const float r = buf[f * 2 + 1];
            CHECK(std::fabs(l) < 1e-5f);
            CHECK(std::fabs(r - 0.5f) < 1e-5f);
        }
    }

    // Mono buffer: no-op (no second channel to balance against).
    {
        AudioFormat mfmt{48000, 1, ChannelLayout::Mono};
        std::vector<float> buf(frames, 0.5f);
        applyPanStereo(AudioSpan{ buf.data(), frames, mfmt }, -1.0f);
        for (float v : buf) {
            CHECK_EQ(v, 0.5f);
        }
    }

    EXIT_WITH_RESULT();
}
