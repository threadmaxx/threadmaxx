#pragma once

/// @file pulse_device.hpp
/// @brief Linux PulseAudio backend (via `libpulse-simple`). Built only when
/// CMake finds the libpulse-simple headers + library at configure time; the
/// `THREADMAXX_AUDIO_HAS_PULSE` macro lets dependent code probe for
/// availability without linking against pulse.
///
/// PImpl'd so this header doesn't pull `<pulse/simple.h>` into every
/// consumer translation unit.

#include "threadmaxx_audio/device.hpp"

#include <memory>

namespace threadmaxx::audio {

/// PulseAudio playback device using the synchronous "simple" API. Opens a
/// playback stream for interleaved float32 LE; `submit()` blocks the caller
/// until the buffer is written or pulse reports an unrecoverable error
/// (which is treated as a silent drop so the mixer keeps running).
class PulseDevice final : public IAudioDevice {
public:
    PulseDevice();
    ~PulseDevice() override;

    PulseDevice(const PulseDevice&)            = delete;
    PulseDevice& operator=(const PulseDevice&) = delete;

    [[nodiscard]] bool initialize(const AudioFormat& format,
                                  std::size_t bufferFrames) override;
    void               shutdown() override;
    void               submit(ConstAudioSpan mixBuffer) override;

    [[nodiscard]] AudioFormat format()       const noexcept override;
    [[nodiscard]] std::size_t bufferFrames() const noexcept override;
    [[nodiscard]] bool        initialized()  const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::audio
