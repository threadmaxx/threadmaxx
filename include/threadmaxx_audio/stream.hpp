#pragma once

/// @file stream.hpp
/// @brief `IAudioStream` — the streaming producer interface, plus a handful
/// of in-process test streams (constant DC, deterministic noise, starved).
///
/// The mixer reads frames from the stream on its own (mix) thread each call;
/// a v1.x batch may add threaded prefetch but v1.0 keeps the simple
/// read-on-mixer-thread contract.

#include "threadmaxx_audio/buffer.hpp"

#include <cstddef>
#include <cstdint>

namespace threadmaxx::audio {

/// Stream producer hook. Returns up to `out.frames` frames written into
/// `out.interleaved`. Returning less than `out.frames`:
///   - if `finished()` is true → end of stream; mixer drops the voice
///     (looping voices call `rewind()` first to continue).
///   - if `finished()` is false → producer underrun; mixer fills the
///     remaining frames with silence and bumps `MixerStats::underruns`.
class IAudioStream {
public:
    virtual ~IAudioStream() = default;

    [[nodiscard]] virtual std::size_t read(AudioSpan out) = 0;
    virtual void rewind() = 0;
    [[nodiscard]] virtual bool finished() const = 0;
    [[nodiscard]] virtual AudioFormat format() const noexcept = 0;
};

/// Deterministic LCG noise source. `totalFrames == 0` is an infinite stream
/// (never finishes); a positive value caps the stream at that many frames
/// and `finished()` returns true once the cursor reaches it.
class NoiseStream final : public IAudioStream {
public:
    NoiseStream(AudioFormat fmt, std::size_t totalFrames = 0, std::uint32_t seed = 1) noexcept
        : format_(fmt), totalFrames_(totalFrames), seed_(seed), seedStart_(seed) {}

    [[nodiscard]] std::size_t read(AudioSpan out) override {
        if (out.format.channels == 0 || out.interleaved == nullptr) return 0;
        std::size_t toRead = out.frames;
        if (totalFrames_ != 0) {
            const std::size_t remaining = (cursor_ >= totalFrames_) ? 0 : (totalFrames_ - cursor_);
            if (toRead > remaining) toRead = remaining;
        }
        for (std::size_t i = 0; i < toRead; ++i) {
            for (std::uint8_t c = 0; c < out.format.channels; ++c) {
                seed_ = seed_ * 1664525u + 1013904223u;
                const float v = (static_cast<float>(seed_) / 4294967295.0f) * 2.0f - 1.0f;
                out.interleaved[i * out.format.channels + c] = v * 0.5f;
            }
        }
        cursor_ += toRead;
        return toRead;
    }
    void rewind() override { cursor_ = 0; seed_ = seedStart_; }
    [[nodiscard]] bool finished() const override {
        return totalFrames_ != 0 && cursor_ >= totalFrames_;
    }
    [[nodiscard]] AudioFormat format() const noexcept override { return format_; }

    [[nodiscard]] std::size_t cursor()      const noexcept { return cursor_; }
    [[nodiscard]] std::size_t totalFrames() const noexcept { return totalFrames_; }

private:
    AudioFormat   format_{};
    std::size_t   totalFrames_ = 0;
    std::size_t   cursor_      = 0;
    std::uint32_t seed_        = 1;
    std::uint32_t seedStart_   = 1;
};

/// Test helper: returns half the requested frames each call and never
/// finishes. Drives the underrun-handling path.
class StarvedStream final : public IAudioStream {
public:
    explicit StarvedStream(AudioFormat fmt) noexcept : format_(fmt) {}

    [[nodiscard]] std::size_t read(AudioSpan out) override {
        if (out.format.channels == 0 || out.interleaved == nullptr) return 0;
        const std::size_t writeFrames = out.frames / 2u;
        for (std::size_t i = 0; i < writeFrames; ++i) {
            for (std::uint8_t c = 0; c < out.format.channels; ++c) {
                out.interleaved[i * out.format.channels + c] = 0.25f;
            }
        }
        cursor_ += writeFrames;
        return writeFrames;
    }
    void rewind() override { cursor_ = 0; }
    [[nodiscard]] bool finished() const override { return false; }
    [[nodiscard]] AudioFormat format() const noexcept override { return format_; }

    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

private:
    AudioFormat format_{};
    std::size_t cursor_ = 0;
};

} // namespace threadmaxx::audio
