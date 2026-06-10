// AU1 — channel-layout enum → channel-count mapping.

#include "Check.hpp"
#include "threadmaxx_audio/threadmaxx_audio.hpp"

int main() {
    using namespace threadmaxx::audio;

    CHECK_EQ(channelCount(ChannelLayout::Mono),      std::uint8_t{1});
    CHECK_EQ(channelCount(ChannelLayout::Stereo),    std::uint8_t{2});
    CHECK_EQ(channelCount(ChannelLayout::Quad),      std::uint8_t{4});
    CHECK_EQ(channelCount(ChannelLayout::FiveOne),   std::uint8_t{6});
    CHECK_EQ(channelCount(ChannelLayout::SevenOne),  std::uint8_t{8});
    CHECK_EQ(channelCount(ChannelLayout::Ambisonic), std::uint8_t{4});

    // AudioFormat equality is a per-field compare.
    AudioFormat a{48000, 2, ChannelLayout::Stereo};
    AudioFormat b{48000, 2, ChannelLayout::Stereo};
    AudioFormat c{44100, 2, ChannelLayout::Stereo};
    AudioFormat d{48000, 1, ChannelLayout::Mono};
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a != d);

    // Handle equality.
    SoundId  s1{17}, s2{17}, s3{18};
    CHECK(s1 == s2);
    CHECK(s1 != s3);

    VoiceId v1{42}, v2{42}, v3{43};
    CHECK(v1 == v2);
    CHECK(v1 != v3);

    EXIT_WITH_RESULT();
}
