#include "threadmaxx_assets/loaders/wav.hpp"

#include <cstdint>
#include <cstring>
#include <string>

#include "threadmaxx_assets/detail/io.hpp"

namespace threadmaxx::assets {

namespace {

template <class T>
T readLE(std::span<const std::byte> bytes, std::size_t off) noexcept {
    T v{};
    std::memcpy(&v, bytes.data() + off, sizeof(T));
    return v;
}

bool chunkIs(const std::byte* p, const char* tag) noexcept {
    return static_cast<char>(p[0]) == tag[0] &&
           static_cast<char>(p[1]) == tag[1] &&
           static_cast<char>(p[2]) == tag[2] &&
           static_cast<char>(p[3]) == tag[3];
}

} // namespace

AssetResult<AudioClipData> parseWav(std::span<const std::byte> bytes,
                                    std::string_view sourcePath) {
    if (bytes.size() < 44) {
        return AssetResult<AudioClipData>::failure(
            ErrorCode::Truncated, "WAV shorter than minimum header");
    }
    if (!chunkIs(bytes.data(), "RIFF") || !chunkIs(bytes.data() + 8, "WAVE")) {
        return AssetResult<AudioClipData>::failure(
            ErrorCode::BadMagic, "missing RIFF/WAVE");
    }

    std::size_t pos = 12;
    std::uint16_t formatTag = 0;
    std::uint16_t channels  = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    bool sawFmt = false;
    std::size_t dataOffset = 0;
    std::uint32_t dataSize = 0;

    while (pos + 8 <= bytes.size()) {
        const std::byte* tag = bytes.data() + pos;
        const auto size = readLE<std::uint32_t>(bytes, pos + 4);
        const std::size_t bodyStart = pos + 8;
        if (bodyStart + size > bytes.size()) {
            return AssetResult<AudioClipData>::failure(
                ErrorCode::Truncated, "WAV chunk truncated");
        }

        if (chunkIs(tag, "fmt ")) {
            if (size < 16) {
                return AssetResult<AudioClipData>::failure(
                    ErrorCode::ParseError, "WAV fmt chunk too short");
            }
            formatTag     = readLE<std::uint16_t>(bytes, bodyStart + 0);
            channels      = readLE<std::uint16_t>(bytes, bodyStart + 2);
            sampleRate    = readLE<std::uint32_t>(bytes, bodyStart + 4);
            // bytesPerSec @ +8, blockAlign @ +12.
            bitsPerSample = readLE<std::uint16_t>(bytes, bodyStart + 14);
            sawFmt = true;
        } else if (chunkIs(tag, "data")) {
            if (!sawFmt) {
                return AssetResult<AudioClipData>::failure(
                    ErrorCode::ParseError, "WAV data before fmt");
            }
            dataOffset = bodyStart;
            dataSize   = size;
            break;
        }
        // Chunks are padded to even bytes.
        pos = bodyStart + size + (size & 1u);
    }

    if (!sawFmt || dataOffset == 0) {
        return AssetResult<AudioClipData>::failure(
            ErrorCode::ParseError, "WAV missing fmt or data chunk");
    }
    if (channels == 0 || channels > 2) {
        return AssetResult<AudioClipData>::failure(
            ErrorCode::UnsupportedFormat, "WAV channels must be 1 or 2");
    }

    SampleFormat sf{SampleFormat::Unknown};
    if (formatTag == 1 && bitsPerSample == 16) {
        sf = SampleFormat::PcmS16;
    } else if (formatTag == 3 && bitsPerSample == 32) {
        sf = SampleFormat::PcmF32;
    } else {
        return AssetResult<AudioClipData>::failure(
            ErrorCode::UnsupportedFormat,
            "WAV: only PCM s16 (tag=1) and float32 (tag=3) supported");
    }

    AudioClipData out;
    out.sampleRate = sampleRate;
    out.channels   = channels;
    out.format     = sf;
    out.sourcePath = std::string(sourcePath);
    out.samples.resize(dataSize);
    std::memcpy(out.samples.data(), bytes.data() + dataOffset, dataSize);

    return AssetResult<AudioClipData>::success(std::move(out));
}

AssetResult<AudioClipData> loadWav(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<AudioClipData>::failure(bytes.code,
                                                   std::move(bytes.message));
    }
    return parseWav(bytes.value, path);
}

} // namespace threadmaxx::assets
