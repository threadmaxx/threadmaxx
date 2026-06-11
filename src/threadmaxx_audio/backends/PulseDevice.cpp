/// @file PulseDevice.cpp
/// @brief PulseAudio implementation via libpulse-simple. Synchronous
/// write API; one `pa_simple_write` call per `submit()`. Errors are
/// silent drops so the mixer thread keeps running even if pulse is
/// momentarily unhappy.

#include "threadmaxx_audio/pulse_device.hpp"

#include <pulse/simple.h>
#include <pulse/error.h>

#include <cstddef>

namespace threadmaxx::audio {

struct PulseDevice::Impl {
    pa_simple*   handle      = nullptr;
    AudioFormat  fmt{};
    std::size_t  bufferFrames = 0;
    bool         initialized  = false;
};

PulseDevice::PulseDevice() : impl_(std::make_unique<Impl>()) {}

PulseDevice::~PulseDevice() {
    if (impl_ && impl_->initialized) shutdown();
}

bool PulseDevice::initialize(const AudioFormat& format, std::size_t bufferFrames) {
    if (!impl_) return false;
    if (format.channels == 0 || bufferFrames == 0 || format.sampleRate == 0) return false;
    if (impl_->initialized) shutdown();

    pa_sample_spec spec{};
    spec.format   = PA_SAMPLE_FLOAT32LE;
    spec.channels = format.channels;
    spec.rate     = format.sampleRate;

    int error = 0;
    impl_->handle = pa_simple_new(
        nullptr,              // default server
        "threadmaxx_audio",   // application name
        PA_STREAM_PLAYBACK,
        nullptr,              // default device
        "playback",           // stream name
        &spec,
        nullptr,              // default channel map
        nullptr,              // default buffer attrs
        &error);
    if (!impl_->handle) return false;

    impl_->fmt          = format;
    impl_->bufferFrames = bufferFrames;
    impl_->initialized  = true;
    return true;
}

void PulseDevice::shutdown() {
    if (!impl_ || !impl_->initialized) return;
    if (impl_->handle) {
        int error = 0;
        pa_simple_flush(impl_->handle, &error);
        pa_simple_free(impl_->handle);
        impl_->handle = nullptr;
    }
    impl_->initialized  = false;
    impl_->bufferFrames = 0;
    impl_->fmt          = AudioFormat{};
}

void PulseDevice::submit(ConstAudioSpan mixBuffer) {
    if (!impl_ || !impl_->initialized || !impl_->handle) return;
    if (mixBuffer.interleaved == nullptr || mixBuffer.frames == 0) return;

    const std::size_t bytes = framesToBytes(mixBuffer.format, mixBuffer.frames);
    int error = 0;
    pa_simple_write(impl_->handle, mixBuffer.interleaved, bytes, &error);
}

AudioFormat  PulseDevice::format()       const noexcept { return impl_ ? impl_->fmt          : AudioFormat{}; }
std::size_t  PulseDevice::bufferFrames() const noexcept { return impl_ ? impl_->bufferFrames : 0; }
bool         PulseDevice::initialized()  const noexcept { return impl_ && impl_->initialized; }

} // namespace threadmaxx::audio
