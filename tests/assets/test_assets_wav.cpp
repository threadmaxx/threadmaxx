#include "Check.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "threadmaxx_assets/loaders/wav.hpp"

using namespace threadmaxx::assets;

namespace {

void appendBytes(std::vector<std::byte>& dst, const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    dst.insert(dst.end(), b, b + n);
}
void appendStr(std::vector<std::byte>& dst, const char* s) {
    for (int i = 0; i < 4; ++i) dst.push_back(static_cast<std::byte>(s[i]));
}
template <class T>
void appendLE(std::vector<std::byte>& dst, T v) {
    appendBytes(dst, &v, sizeof(T));
}

// Build a PCM-16 mono WAV at 8000 Hz with 4 samples.
std::vector<std::byte> buildPcm16Mono() {
    std::vector<std::byte> wav;
    appendStr(wav, "RIFF");
    appendLE<std::uint32_t>(wav, 36 + 8); // file size - 8 (placeholder)
    appendStr(wav, "WAVE");
    appendStr(wav, "fmt ");
    appendLE<std::uint32_t>(wav, 16);
    appendLE<std::uint16_t>(wav, 1);          // format = PCM
    appendLE<std::uint16_t>(wav, 1);          // channels
    appendLE<std::uint32_t>(wav, 8000);       // sampleRate
    appendLE<std::uint32_t>(wav, 8000 * 2);   // bytesPerSec
    appendLE<std::uint16_t>(wav, 2);          // blockAlign
    appendLE<std::uint16_t>(wav, 16);         // bitsPerSample
    appendStr(wav, "data");
    appendLE<std::uint32_t>(wav, 8);          // 4 samples × 2 bytes
    const std::int16_t samples[4] = {0, 1000, -1000, 32000};
    for (auto s : samples) appendLE<std::int16_t>(wav, s);
    return wav;
}

// Build a float32 stereo WAV at 48000 Hz with 2 frames.
std::vector<std::byte> buildF32Stereo() {
    std::vector<std::byte> wav;
    appendStr(wav, "RIFF");
    appendLE<std::uint32_t>(wav, 0);  // size placeholder
    appendStr(wav, "WAVE");
    appendStr(wav, "fmt ");
    appendLE<std::uint32_t>(wav, 16);
    appendLE<std::uint16_t>(wav, 3);
    appendLE<std::uint16_t>(wav, 2);
    appendLE<std::uint32_t>(wav, 48000);
    appendLE<std::uint32_t>(wav, 48000 * 8);
    appendLE<std::uint16_t>(wav, 8);
    appendLE<std::uint16_t>(wav, 32);
    appendStr(wav, "data");
    appendLE<std::uint32_t>(wav, 16); // 2 frames × 2 channels × 4 bytes
    const float values[4] = {0.0f, 1.0f, -1.0f, 0.5f};
    for (auto v : values) appendLE<float>(wav, v);
    return wav;
}

} // namespace

int main() {
    {
        const auto bytes = buildPcm16Mono();
        auto r = parseWav(std::span<const std::byte>(bytes), "mono.wav");
        CHECK(r.ok());
        if (!r.ok()) { EXIT_WITH_RESULT(); }
        const auto& c = r.value;
        CHECK_EQ(c.sampleRate, 8000u);
        CHECK_EQ(c.channels,   1u);
        CHECK(c.format == SampleFormat::PcmS16);
        CHECK_EQ(c.sampleFrames(), 4ull);
        CHECK(std::abs(c.durationSeconds() - 4.0 / 8000.0) < 1e-9);
        // Spot-check first sample.
        std::int16_t s0 = 0;
        std::memcpy(&s0, c.samples.data(), 2);
        CHECK_EQ(s0, 0);
        std::int16_t s3 = 0;
        std::memcpy(&s3, c.samples.data() + 6, 2);
        CHECK_EQ(s3, std::int16_t{32000});
    }

    {
        const auto bytes = buildF32Stereo();
        auto r = parseWav(std::span<const std::byte>(bytes), "f32.wav");
        CHECK(r.ok());
        if (!r.ok()) { EXIT_WITH_RESULT(); }
        const auto& c = r.value;
        CHECK_EQ(c.sampleRate, 48000u);
        CHECK_EQ(c.channels,   2u);
        CHECK(c.format == SampleFormat::PcmF32);
        CHECK_EQ(c.sampleFrames(), 2ull);
        float v0 = 0.0f;
        std::memcpy(&v0, c.samples.data(), 4);
        CHECK(std::abs(v0) < 1e-7f);
        float v1 = 0.0f;
        std::memcpy(&v1, c.samples.data() + 4, 4);
        CHECK(std::abs(v1 - 1.0f) < 1e-7f);
    }

    // Bad magic.
    std::vector<std::byte> bad(44, std::byte{0});
    auto br = parseWav(bad, "");
    CHECK(!br.ok());
    CHECK(br.code == ErrorCode::BadMagic);

    // Unsupported (ADPCM = format tag 2).
    auto unsup = buildPcm16Mono();
    // fmt body starts at offset 20; first u16 there is the format tag.
    unsup[20] = std::byte{2};
    auto ur = parseWav(unsup, "");
    CHECK(!ur.ok());
    CHECK(ur.code == ErrorCode::UnsupportedFormat);

    EXIT_WITH_RESULT();
}
