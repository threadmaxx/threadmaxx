#pragma once

/// @file diagnostics.hpp
/// @brief Non-owning view wrapper over `AudioMixer` for telemetry queries.
/// AU6 surfaces the existing `MixerStats` (now including peak/RMS) and
/// `resetPeaks()`; AU9 adds per-bus summaries for the studio audio panel.

#include "threadmaxx_audio/mixer.hpp"
#include "threadmaxx_audio/types.hpp"

#include <vector>

namespace threadmaxx::audio {

/// AU9 — Per-bus summary POD. One row of the AudioPanel's bus table.
/// Mirrors a subset of `BusDesc` plus the live voice count routing into
/// the bus. POD by design so studios / remote attach can copy it
/// across thread / process boundaries without touching the mixer.
struct BusSummary {
    BusId id{};
    float gainDb = 0.0f;
    bool muted = false;
    bool solo = false;
    bool isMaster = false;
    std::uint32_t voiceCount = 0;
};

class AudioDiagnostics {
public:
    explicit AudioDiagnostics(AudioMixer& mixer) noexcept : mixer_(&mixer) {}

    [[nodiscard]] MixerStats stats() const noexcept { return mixer_->stats(); }

    /// Clear the peak-hold meters (`peakL` / `peakR`). Does NOT reset
    /// `underruns` or `droppedVoices` — those are cumulative-since-start
    /// counters by design.
    void resetPeaks() noexcept { mixer_->resetPeaks(); }

    /// AU9 — enumerate every live bus + the master. Pass-through to
    /// `AudioMixer::listBuses`; one allocation per call (caller owns
    /// the returned vector).
    [[nodiscard]] std::vector<BusSummary> listBuses() const {
        return mixer_->listBuses();
    }

private:
    AudioMixer* mixer_;
};

} // namespace threadmaxx::audio
