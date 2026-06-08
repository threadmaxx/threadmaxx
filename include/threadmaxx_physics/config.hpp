#pragma once

#include <cstdint>

/// Solver options that apply at scene construction time.
///
/// `PhysicsConfig` is passed through `PhysicsScene` to the backend at
/// `createWorld` time. P1 only defines the fields; later batches wire
/// them into stepping (P4) and queries (P5).
namespace threadmaxx::physics {

/// Configuration knobs that don't change per body. Trivially-copyable
/// so it round-trips through serialization unchanged.
struct PhysicsConfig {
    /// Fixed timestep the scene targets, in seconds. Backends may
    /// substep internally — this is the outer cadence the engine
    /// drives `step()` at.
    float fixedTimestep{1.0f / 60.0f};

    /// Maximum substeps a single `step()` call is allowed to take.
    /// Caps the simulation cost when the wall-clock dt spikes.
    std::uint32_t maxSubSteps{4};

    /// Default gravity vector applied to dynamic bodies (m/s²). Real
    /// backends honor this; the StubBackend ignores it because the
    /// stub never integrates forces.
    float gravityX{0.0f};
    float gravityY{-9.81f};
    float gravityZ{0.0f};

    /// Whether the backend is permitted to use multi-threading
    /// internally. Lockstep / deterministic profiles set this to
    /// false (see v1.x "Determinism profile" in FUTURE_WORK).
    bool allowSolverThreading{true};
};

} // namespace threadmaxx::physics
