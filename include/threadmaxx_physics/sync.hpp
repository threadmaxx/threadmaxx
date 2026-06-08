#pragma once

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/body.hpp"
#include "threadmaxx_physics/types.hpp"

#include <span>
#include <vector>

/// Engine ↔ physics body-state sync helpers.
///
/// `IPhysicsBackend::syncBodiesToGame` is the low-level batch interface
/// every backend implements: hand it a span of `BodyId`s and a parallel
/// span of `BodyState`s, and the backend fills the output in place.
/// The free functions in this header are ergonomic wrappers around that
/// virtual — game code typically wants either:
///
/// - a "fill this buffer I already own" form (for steady-state per-tick
///   sync into a system-owned scratch buffer that lives across ticks);
/// - or a "give me a fresh vector" form (for one-off snapshots).
///
/// Both paths funnel through the same backend virtual; the wrappers
/// just shape the input / output.
///
/// All helpers are sim-thread-only by convention — they call into the
/// backend directly and inherit its threading contract (see
/// `IPhysicsBackend` doc).
namespace threadmaxx::physics {

/// Fill `outStates` with the per-tick `BodyState` of every entry in
/// `bodies`, in order. The two spans MUST be the same size; on
/// mismatch the call is a no-op and the output is left untouched.
/// Entries whose `BodyId` is stale / invalid receive a
/// default-constructed `BodyState`.
inline void syncBodyStates(IPhysicsBackend& backend,
                           PhysicsWorldId world,
                           std::span<const BodyId> bodies,
                           std::span<BodyState> outStates) {
    backend.syncBodiesToGame(world, bodies, outStates);
}

/// Convenience form that allocates a fresh `std::vector<BodyState>`
/// sized to match `bodies` and returns it by value. Prefer the span
/// overload in hot paths — game systems usually keep a per-system
/// scratch vector and `assign`/`resize` it across ticks to avoid the
/// per-tick allocation this form makes.
inline std::vector<BodyState> syncBodyStates(IPhysicsBackend& backend,
                                             PhysicsWorldId world,
                                             std::span<const BodyId> bodies) {
    std::vector<BodyState> out(bodies.size());
    backend.syncBodiesToGame(world, bodies, std::span<BodyState>(out));
    return out;
}

} // namespace threadmaxx::physics
