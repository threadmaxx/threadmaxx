#pragma once

/// @file diagnostics.hpp
/// @brief P10 — Diagnostics snapshot for the physics subsystem.
///
/// `PhysicsWorldStats` is a POD bundling the per-world counters a
/// debug HUD / studio inspector typically wants. Backends opt in via
/// `IPhysicsBackend::worldStats`; the default implementation returns
/// zeros so backends predating P10 keep compiling.

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cstddef>
#include <cstdint>

namespace threadmaxx::physics {

/// One-tick snapshot of a single physics world.
struct PhysicsWorldStats {
    /// Live (non-destroyed) body count.
    std::size_t bodyCount = 0;
    /// Live (non-destroyed) constraint count.
    std::size_t constraintCount = 0;
    /// Active contact pairs at the end of the most recent
    /// `stepWorld`.
    std::size_t activeContactCount = 0;
};

/// Sample a `PhysicsWorldStats` from a backend. Pass-through to
/// `IPhysicsBackend::worldStats`; provided as a free function so the
/// studio panel call site mirrors the navmesh / audio shape.
inline PhysicsWorldStats sampleWorldStats(IPhysicsBackend& backend,
                                          PhysicsWorldId world) noexcept {
    return backend.worldStats(world);
}

} // namespace threadmaxx::physics
