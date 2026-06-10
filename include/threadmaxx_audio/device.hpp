#pragma once

/// @file device.hpp
/// @brief `IAudioDevice` — the backend interface that the mixer submits its
/// final mixed buffer into.
///
/// AU1 ships the interface plus a `LoopbackDevice` test backend. AU8 ships
/// the real platform backends (ALSA / PulseAudio). The mixer (AU2) never
/// names a concrete backend; everything flows through `IAudioDevice`.

#include "threadmaxx_audio/buffer.hpp"

#include <cstddef>

namespace threadmaxx::audio {

/// Backend-agnostic audio output device.
///
/// @thread_safety The mixer is the only caller. `submit()` runs on the
/// mixer's callback thread (or the sim thread in tests); `initialize()` /
/// `shutdown()` run on the owning thread. Concrete backends must not assume
/// these threads are the same.
///
/// @pre `initialize()` must succeed before the first `submit()`. After
/// `shutdown()`, calls to `submit()` are ignored (the contract is "no
/// abort; the device silently drops"). `format()` and `bufferFrames()` are
/// valid both before and after `initialize()` — they reflect what the device
/// will accept or last accepted.
class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;

    /// Bring the device up at the requested format + buffer size.
    /// Returns true on success; on failure, the device stays uninitialized
    /// and subsequent `submit()` calls are no-ops.
    [[nodiscard]] virtual bool initialize(const AudioFormat& format,
                                          std::size_t bufferFrames) = 0;

    /// Tear the device down. Idempotent — calling on an already-shut-down
    /// device is a no-op.
    virtual void shutdown() = 0;

    /// Push a mixed buffer to the device. Must be called with a buffer whose
    /// format matches `format()` and whose frame count equals
    /// `bufferFrames()`. Mismatches are backend-defined — the LoopbackDevice
    /// asserts; production backends may resample.
    virtual void submit(ConstAudioSpan mixBuffer) = 0;

    /// The format `initialize()` was called with (or the default if not
    /// initialized). Stable after a successful initialize.
    [[nodiscard]] virtual AudioFormat format() const noexcept = 0;

    /// The frame count `initialize()` was called with. Backends MUST submit
    /// exactly this many frames per call.
    [[nodiscard]] virtual std::size_t bufferFrames() const noexcept = 0;
};

} // namespace threadmaxx::audio
