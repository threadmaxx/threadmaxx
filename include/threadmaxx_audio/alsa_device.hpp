#pragma once

/// @file alsa_device.hpp
/// @brief Linux ALSA backend. Built only when CMake finds ALSA at configure
/// time; the `THREADMAXX_AUDIO_HAS_ALSA` macro lets dependent code probe
/// for availability without linking against ALSA itself.
///
/// PImpl'd so this header doesn't pull `<alsa/asoundlib.h>` into every
/// consumer translation unit.

#include "threadmaxx_audio/device.hpp"

#include <memory>

namespace threadmaxx::audio {

/// ALSA playback device. Opens the system "default" PCM, configures it for
/// interleaved 32-bit float playback, and sends every `submit()` buffer
/// through `snd_pcm_writei`. Recovers from a transient underrun via
/// `snd_pcm_recover` once; unrecoverable failures are silent drops so the
/// mixer keeps running.
class AlsaDevice final : public IAudioDevice {
public:
    AlsaDevice();
    ~AlsaDevice() override;

    AlsaDevice(const AlsaDevice&)            = delete;
    AlsaDevice& operator=(const AlsaDevice&) = delete;

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
