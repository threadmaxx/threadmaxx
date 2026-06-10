#pragma once

/// @file dsp.hpp
/// @brief Standalone DSP helpers — gain / pan / fade ops over `AudioSpan`.
/// All header-only, zero-allocation, no internal state. The mixer uses these
/// privately; game code can also chain them on its own buffers.

#include "threadmaxx_audio/buffer.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace threadmaxx::audio {

/// Apply a single gain factor (in dB) to every sample in `buffer`. 0 dB is a
/// bit-exact no-op (IEEE-754 `x * 1.0f == x`). -∞ dB collapses every sample
/// to zero. Acts on every channel uniformly.
inline void applyGain(AudioSpan buffer, float gainDb) noexcept {
    if (buffer.interleaved == nullptr || buffer.frames == 0 || buffer.format.channels == 0) return;
    const float linear = std::pow(10.0f, gainDb * 0.05f);
    if (linear == 1.0f) return;
    const std::size_t n = samplesIn(buffer.format, buffer.frames);
    for (std::size_t i = 0; i < n; ++i) buffer.interleaved[i] *= linear;
}

/// Apply equal-power stereo panning. `pan = -1` is full left, `pan = 0` is
/// equal-power center (both channels at `√2/2`), `pan = +1` is full right.
/// No-op on buffers with fewer than 2 channels (mono has no stereo image).
/// Channels beyond L/R are left untouched.
inline void applyPanStereo(AudioSpan buffer, float pan) noexcept {
    if (buffer.interleaved == nullptr || buffer.frames == 0) return;
    if (buffer.format.channels < 2) return;
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    constexpr float kQuarterPi = 0.785398163397448309616f;
    const float angle = (pan + 1.0f) * kQuarterPi;
    const float panL  = std::cos(angle);
    const float panR  = std::sin(angle);
    const std::uint8_t ch = buffer.format.channels;
    for (std::size_t f = 0; f < buffer.frames; ++f) {
        float* d = buffer.interleaved + f * ch;
        d[0] *= panL;
        d[1] *= panR;
    }
}

/// Linear fade-in over the first `seconds * sampleRate` frames. Sample 0 is
/// silenced (× 0), sample `seconds*sampleRate - 1` reaches unity (× 1), and
/// every subsequent sample passes through unchanged.
inline void applyFadeIn(AudioSpan buffer, float seconds, float sampleRate) noexcept {
    if (buffer.interleaved == nullptr || buffer.frames == 0 || buffer.format.channels == 0) return;
    if (seconds <= 0.0f || sampleRate <= 0.0f) return;
    const std::size_t fadeFrames = static_cast<std::size_t>(seconds * sampleRate);
    if (fadeFrames < 2u) return;
    const std::size_t denom = fadeFrames - 1u;
    const float denomF      = static_cast<float>(denom);
    const std::uint8_t ch   = buffer.format.channels;
    for (std::size_t f = 0; f < buffer.frames; ++f) {
        const float t = (f >= denom) ? 1.0f : static_cast<float>(f) / denomF;
        float* d = buffer.interleaved + f * ch;
        for (std::uint8_t c = 0; c < ch; ++c) d[c] *= t;
    }
}

/// Linear fade-out over the first `seconds * sampleRate` frames. Sample 0
/// passes through unchanged (× 1), sample `seconds*sampleRate - 1` reaches
/// zero, and every subsequent sample is silenced.
inline void applyFadeOut(AudioSpan buffer, float seconds, float sampleRate) noexcept {
    if (buffer.interleaved == nullptr || buffer.frames == 0 || buffer.format.channels == 0) return;
    if (seconds <= 0.0f || sampleRate <= 0.0f) return;
    const std::size_t fadeFrames = static_cast<std::size_t>(seconds * sampleRate);
    if (fadeFrames < 2u) return;
    const std::size_t denom = fadeFrames - 1u;
    const float denomF      = static_cast<float>(denom);
    const std::uint8_t ch   = buffer.format.channels;
    for (std::size_t f = 0; f < buffer.frames; ++f) {
        const float t = (f >= denom) ? 0.0f : 1.0f - static_cast<float>(f) / denomF;
        float* d = buffer.interleaved + f * ch;
        for (std::uint8_t c = 0; c < ch; ++c) d[c] *= t;
    }
}

} // namespace threadmaxx::audio
