#pragma once

/// @file diagnostics.hpp
/// @brief Non-owning view wrapper over `AudioMixer` for telemetry queries.
/// AU6 surfaces the existing `MixerStats` (now including peak/RMS) and
/// `resetPeaks()`; future batches can extend this view without disturbing
/// the mixer's public surface.

#include "threadmaxx_audio/mixer.hpp"

namespace threadmaxx::audio {

class AudioDiagnostics {
public:
    explicit AudioDiagnostics(AudioMixer& mixer) noexcept : mixer_(&mixer) {}

    [[nodiscard]] MixerStats stats() const noexcept { return mixer_->stats(); }

    /// Clear the peak-hold meters (`peakL` / `peakR`). Does NOT reset
    /// `underruns` or `droppedVoices` — those are cumulative-since-start
    /// counters by design.
    void resetPeaks() noexcept { mixer_->resetPeaks(); }

private:
    AudioMixer* mixer_;
};

} // namespace threadmaxx::audio
