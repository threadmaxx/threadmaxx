#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace threadmaxx::assets {

enum class SampleFormat : std::uint8_t {
    Unknown = 0,
    PcmS16,
    PcmF32
};

[[nodiscard]] constexpr std::uint32_t bytesPerSample(SampleFormat f) noexcept {
    switch (f) {
        case SampleFormat::PcmS16: return 2;
        case SampleFormat::PcmF32: return 4;
        case SampleFormat::Unknown: break;
    }
    return 0;
}

struct AudioClipData {
    std::uint32_t           sampleRate{};
    std::uint16_t           channels{};
    SampleFormat            format{SampleFormat::Unknown};
    std::vector<std::byte>  samples;
    std::string             sourcePath;

    [[nodiscard]] std::uint64_t sampleFrames() const noexcept {
        const std::uint32_t bps = bytesPerSample(format);
        if (bps == 0 || channels == 0) {
            return 0;
        }
        const std::uint64_t frameBytes =
            static_cast<std::uint64_t>(bps) * static_cast<std::uint64_t>(channels);
        return frameBytes == 0 ? 0 : samples.size() / frameBytes;
    }

    [[nodiscard]] double durationSeconds() const noexcept {
        if (sampleRate == 0) {
            return 0.0;
        }
        return static_cast<double>(sampleFrames()) / static_cast<double>(sampleRate);
    }
};

} // namespace threadmaxx::assets
