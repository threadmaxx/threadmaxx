#pragma once

/// @file loopback_device.hpp
/// @brief `LoopbackDevice` — deterministic test backend.
///
/// Captures every submitted mix buffer into an in-memory vector so tests can
/// assert on the bytes that would have gone to hardware. No threads, no real
/// I/O, no allocations on `submit()` after the first capture allocates the
/// vector to `bufferFrames * channels`.

#include "threadmaxx_audio/device.hpp"

#include <cstddef>
#include <vector>

namespace threadmaxx::audio {

/// In-memory device that records every submitted mix buffer.
///
/// Use in tests that need to assert on the mixer's final output without
/// taking a hardware-device dependency. The captured buffer chain is
/// `capturedBuffers()` — one `std::vector<float>` per `submit()` call.
class LoopbackDevice final : public IAudioDevice {
public:
    bool initialize(const AudioFormat& format, std::size_t bufferFrames) override;
    void shutdown() override;
    void submit(ConstAudioSpan mixBuffer) override;

    [[nodiscard]] AudioFormat format()       const noexcept override { return format_; }
    [[nodiscard]] std::size_t bufferFrames() const noexcept override { return bufferFrames_; }
    [[nodiscard]] bool        initialized()  const noexcept          { return initialized_; }

    /// One captured buffer per `submit()` call, in submission order.
    [[nodiscard]] const std::vector<std::vector<float>>& capturedBuffers() const noexcept {
        return captured_;
    }

    /// Total submitted-frame count across every captured buffer. Convenience
    /// for tests that don't care about individual buffer boundaries.
    [[nodiscard]] std::size_t totalFramesCaptured() const noexcept;

    /// Drop every captured buffer. Initialization state is preserved.
    void clearCaptured() noexcept { captured_.clear(); }

private:
    AudioFormat                       format_{};
    std::size_t                       bufferFrames_ = 0;
    bool                              initialized_  = false;
    std::vector<std::vector<float>>   captured_;
};

} // namespace threadmaxx::audio
