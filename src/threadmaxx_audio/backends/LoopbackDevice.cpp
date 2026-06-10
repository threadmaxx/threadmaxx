/// @file LoopbackDevice.cpp
/// @brief Implementation of the AU1 in-memory test backend.
///
/// Pre-allocates the per-`submit` capture vector up front to keep the
/// post-warmup `submit` path zero-allocation under the tracking allocator
/// used in AU2's `test_audio_mixer_no_allocations`.

#include "threadmaxx_audio/loopback_device.hpp"

#include <cassert>
#include <cstddef>

namespace threadmaxx::audio {

bool LoopbackDevice::initialize(const AudioFormat& format, std::size_t bufferFrames) {
    if (bufferFrames == 0 || format.channels == 0) {
        return false;
    }
    format_       = format;
    bufferFrames_ = bufferFrames;
    initialized_  = true;
    captured_.clear();
    return true;
}

void LoopbackDevice::shutdown() {
    initialized_  = false;
    bufferFrames_ = 0;
    format_       = AudioFormat{};
}

void LoopbackDevice::submit(ConstAudioSpan mixBuffer) {
    if (!initialized_) {
        return; // silently drop — contract: "no abort after shutdown".
    }
    assert(mixBuffer.format == format_ && "submit format mismatches initialize format");
    assert(mixBuffer.frames == bufferFrames_ && "submit frame count mismatches bufferFrames");

    const std::size_t sampleCount = samplesIn(mixBuffer.format, mixBuffer.frames);
    captured_.emplace_back();
    captured_.back().assign(mixBuffer.interleaved,
                            mixBuffer.interleaved + sampleCount);
}

std::size_t LoopbackDevice::totalFramesCaptured() const noexcept {
    std::size_t total = 0;
    for (const auto& buf : captured_) {
        if (format_.channels == 0) continue;
        total += buf.size() / static_cast<std::size_t>(format_.channels);
    }
    return total;
}

} // namespace threadmaxx::audio
