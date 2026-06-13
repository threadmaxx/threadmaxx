#pragma once

/// @file profiler.hpp
/// @brief E13 — live profiler view over `FrameSnapshot` streams.
///
/// `ProfilerView` collects engine `FrameSnapshot` rows into a ring
/// buffer and rolls per-system aggregates. The studio
/// `ProfilerPanel` reads `summary()` once per render — there is no
/// active drawing here; this is pure data.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <threadmaxx/Trace.hpp>

namespace threadmaxx::editor {

/// @brief One system's profiler rollup over the captured window.
struct ProfilerSystemRow {
    std::string   name;
    double        avgUpdateSeconds{0.0};
    double        maxUpdateSeconds{0.0};
    double        totalUpdateSeconds{0.0};
    /// Total wait time accumulated across the window. Combined with
    /// `totalUpdateSeconds` it surfaces "this system spends 60% of
    /// its update time idle" without forcing the panel to walk every
    /// captured sample.
    double        totalWaitSeconds{0.0};
    std::uint64_t sampleCount{0};
    std::uint32_t peakQueueDepth{0};
};

/// @brief Window-aggregated profiler summary.
struct ProfilerSummary {
    /// Number of captured snapshots in the window.
    std::uint64_t                   samples{0};
    /// `EngineStats::tick` of the oldest / newest snapshot.
    std::uint64_t                   firstTick{0};
    std::uint64_t                   lastTick{0};
    double                          avgStepSeconds{0.0};
    double                          minStepSeconds{0.0};
    double                          maxStepSeconds{0.0};
    /// One row per system that appeared at least once in the window,
    /// sorted by `avgUpdateSeconds` descending (the panel's top-N
    /// table can take the first N rows directly).
    std::vector<ProfilerSystemRow>  systems;
};

/// @brief Bounded ring of `FrameSnapshot` rollups.
///
/// `onFrame(snap)` copies the headline (`EngineStats`) and the
/// per-system rows out of the borrowed span. The system rows are
/// keyed by name so a registration-order change between snapshots
/// doesn't fragment the rollup.
///
/// @thread_safety Single-threaded by design. The studio panel
///                snapshot-collects on the sim thread (mirroring
///                `HudTraceSink`); cross-thread fan-out is out of
///                scope.
class ProfilerView {
public:
    explicit ProfilerView(std::size_t capacity = 256) noexcept;

    /// @brief Append one captured snapshot. Drops the oldest row
    /// when at capacity. Names are owned per-row in the system map.
    void onFrame(const threadmaxx::FrameSnapshot& snap);

    /// @brief Aggregate every buffered snapshot into a single
    /// summary. O(samples + systems).
    [[nodiscard]] ProfilerSummary summary() const;

    /// @brief Drop every buffered snapshot.
    void clear() noexcept;

    [[nodiscard]] std::size_t capacity()    const noexcept { return capacity_; }
    [[nodiscard]] std::size_t sampleCount() const noexcept { return samples_.size(); }

    /// @brief Resize the ring. Older samples beyond the new cap are
    /// dropped (oldest-first); new cap of 0 is rejected (kept at 1).
    void setCapacity(std::size_t cap) noexcept;

private:
    struct SystemSample {
        std::string   name;
        double        updateSeconds{0.0};
        double        waitSeconds{0.0};
        std::uint32_t peakQueueDepth{0};
    };
    struct StepSample {
        std::uint64_t              tick{0};
        double                     stepSeconds{0.0};
        std::vector<SystemSample>  systems;
    };

    std::size_t            capacity_;
    std::deque<StepSample> samples_;
};

} // namespace threadmaxx::editor
