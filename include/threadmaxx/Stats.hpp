#pragma once

#include <cstddef>
#include <cstdint>

namespace threadmaxx {

// Snapshot of engine instrumentation, refreshed at the end of each step().
// Cheap to copy. Read via Engine::stats().
struct EngineStats {
    // Tick this snapshot describes (the tick that just finished). 0 before
    // the first step() runs.
    std::uint64_t tick = 0;

    // Wall-clock duration of the most recent step(), in seconds.
    double lastStepSeconds = 0.0;

    // Exponentially weighted moving average of step duration. Decay factor
    // is fixed at 1/16, so this tracks a ~16-step horizon.
    double avgStepSeconds = 0.0;

    // Jobs handed to JobSystem during the most recent step(). Each
    // parallelFor chunk counts as one job; single() does not submit.
    std::uint64_t jobsSubmittedLastStep = 0;

    // Commands committed during the most recent step(), summed across all
    // systems' command buffers.
    std::uint64_t commandsCommittedLastStep = 0;

    // Live entity count after the most recent commit.
    std::size_t aliveEntities = 0;

    // Lifetime totals since initialize().
    std::uint64_t totalTicks = 0;
    std::uint64_t totalJobsSubmitted = 0;
    std::uint64_t totalCommandsCommitted = 0;
};

} // namespace threadmaxx
