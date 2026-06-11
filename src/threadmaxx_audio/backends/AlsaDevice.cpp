/// @file AlsaDevice.cpp
/// @brief ALSA implementation. Default "default" PCM, 32-bit float
/// interleaved, target latency 100 ms. Transient underruns are recovered
/// via `snd_pcm_recover`; unrecoverable failures drop the buffer silently
/// so the mixer thread keeps making forward progress.

#include "threadmaxx_audio/alsa_device.hpp"

#include <alsa/asoundlib.h>

#include <cstddef>

namespace threadmaxx::audio {

struct AlsaDevice::Impl {
    snd_pcm_t*   handle      = nullptr;
    AudioFormat  fmt{};
    std::size_t  bufferFrames = 0;
    bool         initialized  = false;
};

AlsaDevice::AlsaDevice() : impl_(std::make_unique<Impl>()) {}

AlsaDevice::~AlsaDevice() {
    if (impl_ && impl_->initialized) shutdown();
}

bool AlsaDevice::initialize(const AudioFormat& format, std::size_t bufferFrames) {
    if (!impl_) return false;
    if (format.channels == 0 || bufferFrames == 0 || format.sampleRate == 0) return false;
    if (impl_->initialized) shutdown();

    if (snd_pcm_open(&impl_->handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        impl_->handle = nullptr;
        return false;
    }

    const int err = snd_pcm_set_params(
        impl_->handle,
        SND_PCM_FORMAT_FLOAT_LE,
        SND_PCM_ACCESS_RW_INTERLEAVED,
        static_cast<unsigned int>(format.channels),
        static_cast<unsigned int>(format.sampleRate),
        1,        // soft resample if backend disagrees
        100000);  // 100 ms target latency
    if (err < 0) {
        snd_pcm_close(impl_->handle);
        impl_->handle = nullptr;
        return false;
    }

    impl_->fmt          = format;
    impl_->bufferFrames = bufferFrames;
    impl_->initialized  = true;
    return true;
}

void AlsaDevice::shutdown() {
    if (!impl_ || !impl_->initialized) return;
    if (impl_->handle) {
        snd_pcm_drain(impl_->handle);
        snd_pcm_close(impl_->handle);
        impl_->handle = nullptr;
    }
    impl_->initialized  = false;
    impl_->bufferFrames = 0;
    impl_->fmt          = AudioFormat{};
}

void AlsaDevice::submit(ConstAudioSpan mixBuffer) {
    if (!impl_ || !impl_->initialized || !impl_->handle) return;
    if (mixBuffer.interleaved == nullptr || mixBuffer.frames == 0) return;

    snd_pcm_sframes_t written =
        snd_pcm_writei(impl_->handle, mixBuffer.interleaved,
                       static_cast<snd_pcm_uframes_t>(mixBuffer.frames));
    if (written < 0) {
        // Single attempt at recovery (transient xrun); unrecoverable
        // failure means the device is gone — silent drop.
        snd_pcm_recover(impl_->handle, static_cast<int>(written), 1);
    }
}

AudioFormat  AlsaDevice::format()       const noexcept { return impl_ ? impl_->fmt          : AudioFormat{}; }
std::size_t  AlsaDevice::bufferFrames() const noexcept { return impl_ ? impl_->bufferFrames : 0; }
bool         AlsaDevice::initialized()  const noexcept { return impl_ && impl_->initialized; }

} // namespace threadmaxx::audio
