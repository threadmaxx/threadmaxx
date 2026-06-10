// AU1 — AudioSpan / ConstAudioSpan round-trip + framesToBytes exactness.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

int main() {
    using namespace threadmaxx::audio;

    // framesToBytes is exact across mono / stereo / 5.1.
    AudioFormat mono   { 48000, 1, ChannelLayout::Mono };
    AudioFormat stereo { 48000, 2, ChannelLayout::Stereo };
    AudioFormat fiveOne{ 48000, 6, ChannelLayout::FiveOne };

    CHECK_EQ(framesToBytes(mono,    1024), std::size_t{1024 * 1 * sizeof(float)});
    CHECK_EQ(framesToBytes(stereo,  1024), std::size_t{1024 * 2 * sizeof(float)});
    CHECK_EQ(framesToBytes(fiveOne, 1024), std::size_t{1024 * 6 * sizeof(float)});
    CHECK_EQ(framesToBytes(stereo, 0),     std::size_t{0});

    // samplesIn returns frames * channels.
    CHECK_EQ(samplesIn(stereo, 1024),  std::size_t{2048});
    CHECK_EQ(samplesIn(fiveOne, 256),  std::size_t{1536});

    // AudioSpan / ConstAudioSpan iterate the same interleaved layout.
    std::vector<float> buf(2048, 0.0f);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<float>(i) * 0.5f;
    }

    AudioSpan span{buf.data(), 1024, stereo};
    CHECK_EQ(span.frames, std::size_t{1024});
    CHECK(span.format == stereo);
    CHECK(span.interleaved == buf.data());

    // Sum of the buffer via the AudioSpan view matches direct iteration.
    double viaSpan = 0.0;
    for (std::size_t i = 0; i < samplesIn(span.format, span.frames); ++i) {
        viaSpan += static_cast<double>(span.interleaved[i]);
    }
    double viaDirect = 0.0;
    for (float v : buf) viaDirect += static_cast<double>(v);
    CHECK(viaSpan == viaDirect);

    // ConstAudioSpan is a read-only view; same shape, const pointer.
    ConstAudioSpan cspan{buf.data(), 1024, stereo};
    CHECK_EQ(cspan.frames, std::size_t{1024});
    CHECK(cspan.format == stereo);
    CHECK(cspan.interleaved == buf.data());

    // The byte size reported by framesToBytes matches a memcpy through the
    // span's interleaved pointer.
    std::vector<float> dst(2048, 0.0f);
    std::memcpy(dst.data(), span.interleaved, framesToBytes(span.format, span.frames));
    CHECK(dst == buf);

    EXIT_WITH_RESULT();
}
