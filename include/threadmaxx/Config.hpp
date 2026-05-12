#pragma once

#include <cstdint>

namespace threadmaxx {

struct Config {
    // 0 means "pick a reasonable default based on hardware concurrency".
    std::uint32_t workerCount = 0;

    // Fixed simulation step in seconds. 60 Hz by default.
    double fixedStepSeconds = 1.0 / 60.0;

    // Maximum number of fixed steps that run() will execute per outer
    // iteration when catching up to wall-clock. Prevents the spiral of death.
    std::uint32_t maxStepsPerIteration = 8;

    // When true, the engine guarantees the same world state for the same
    // inputs across runs and machines: jobs are committed strictly in
    // submission order and workers never observe shared mutable state.
    bool deterministic = false;

    // If true, run() sleeps to match wall-clock. If false, run() iterates
    // as fast as possible (useful for tests and offline simulation).
    bool sleepToPace = true;

    // Initial entity capacity hint. Storage grows past this on demand.
    std::uint32_t initialEntityCapacity = 1024;
};

} // namespace threadmaxx
