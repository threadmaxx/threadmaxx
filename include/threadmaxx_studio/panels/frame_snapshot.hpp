#pragma once

/// @file panels/frame_snapshot.hpp
/// @brief `FrameSnapshotPanel` — live FPS + frame-time histogram.
///
/// Doubles as an `ITraceSink` so a host can wire it straight into
/// `Engine::setTraceSink`. Maintains a fixed-size ring buffer of the
/// most recent `lastStepSeconds` values; `render()` emits FPS,
/// average step time, and one row per histogram bin.

#include "../panel.hpp"

#include <threadmaxx/Telemetry.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace threadmaxx::studio {

class FrameSnapshotPanel : public IStudioPanel,
                           public threadmaxx::ITraceSink {
public:
    /// @brief Construct with a ring-buffer capacity (default 60 frames
    /// — roughly one second at 60 Hz).
    explicit FrameSnapshotPanel(std::size_t historyCapacity = 60);

    // ITraceSink — called once per Engine::step on the sim thread.
    void onFrame(const threadmaxx::FrameSnapshot& snap) override;

    // IStudioPanel.
    std::string_view id() const noexcept override {
        return "engine.frame_snapshot";
    }
    std::string_view title() const noexcept override {
        return "Frame Snapshot";
    }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Number of samples currently held in the ring (capped at
    /// `historyCapacity`).
    std::size_t sampleCount() const noexcept { return sampleCount_; }

    /// @brief Tick of the most recent observed snapshot.
    std::uint64_t latestTick() const noexcept { return latestTick_; }

    /// @brief Most recent `lastStepSeconds` value.
    double latestStepSeconds() const noexcept { return latestStepSeconds_; }

    /// @brief Quick FPS view (round-half-up of 1/latestStepSeconds);
    /// returns 0 when no sample has been published.
    int latestFps() const noexcept;

private:
    std::vector<double> ring_;
    std::size_t writeIdx_{0};
    std::size_t sampleCount_{0};
    std::uint64_t latestTick_{0};
    double latestStepSeconds_{0.0};
};

} // namespace threadmaxx::studio
