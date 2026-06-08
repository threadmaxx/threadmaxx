#pragma once

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cstdint>
#include <functional>
#include <utility>

/// Contact event notifications.
///
/// `setContactCallback(world, cb)` installs a per-world callback invoked
/// synchronously during `stepWorld` whenever two bodies' collision
/// volumes change overlap state. Two phases are reported:
///
/// - **Begin** — first tick in which a pair overlaps. Fires exactly
///   once per overlap episode regardless of how long the overlap lasts.
/// - **End**   — first tick in which a previously-overlapping pair no
///   longer overlaps. Also fires exactly once.
///
/// Continuing-overlap notifications ("Persist") are intentionally NOT
/// emitted — game code that wants per-tick contact processing can run
/// `overlapBodies` on its own schedule. Skipping Persist keeps the
/// callback rate proportional to gameplay events, not frame count.
///
/// **Pair canonicalization.** Each event carries `bodyA` and `bodyB`
/// such that `bodyA.value < bodyB.value` regardless of which order the
/// bodies were created in. This guarantees a single Begin per overlap
/// even if the backend enumerates pairs in different orders across ticks.
///
/// **Body destruction.** When a body involved in an active contact is
/// destroyed via `IPhysicsBackend::destroyBody`, the backend fires an
/// End event for every active pair touching that body BEFORE bumping the
/// body's generation. The event carries the still-valid pre-destroy
/// `BodyId`, so the callback can look up game-side bookkeeping one last
/// time. After the destroyBody call returns the `BodyId` is stale and
/// any subsequent lookup returns nullopt.
///
/// **Stub backend semantics.** The `StubBackend` answers contact
/// detection using the same world-space AABB it uses for queries (per
/// `query.hpp`'s file-level doc). Rotation is ignored at the stub —
/// real backends (P9) use the solver's broadphase + narrowphase. Layer
/// filtering is NOT applied to contacts at the stub: any two
/// overlapping bodies fire events regardless of layer. Real backends
/// apply their layer-pair matrix; game code that needs layer-aware
/// contacts at the stub can filter inside the callback.
///
/// All helpers are sim-thread-only by convention — they call into the
/// backend directly and inherit its threading contract.
namespace threadmaxx::physics {

/// Phase of a contact event. `Begin` fires once per overlap episode;
/// `End` fires once when the overlap dissolves.
enum class ContactPhase : std::uint8_t {
    Begin = 0,
    End   = 1,
};

/// Per-event payload. `bodyA` and `bodyB` are canonicalized so
/// `bodyA.value < bodyB.value`.
struct ContactEvent {
    ContactPhase phase{ContactPhase::Begin};
    BodyId bodyA{};
    BodyId bodyB{};
};

/// Callback type for contact events. Backend invokes synchronously from
/// `stepWorld` (or from `destroyBody` for the End-on-destroy path).
/// Pass a default-constructed `ContactCallback{}` to clear an installed
/// callback.
using ContactCallback = std::function<void(const ContactEvent&)>;

/// Install (or clear, when `callback` is empty) the per-world contact
/// callback. The backend takes ownership of the function object.
/// Replacing an already-installed callback drops the previous one
/// without firing any synthetic End events for the currently-active
/// contacts — the new callback simply picks up at the next stepWorld.
inline void setContactCallback(IPhysicsBackend& backend,
                               PhysicsWorldId world,
                               ContactCallback callback) {
    backend.setContactCallback(world, std::move(callback));
}

/// Convenience: detach the callback. Equivalent to
/// `setContactCallback(backend, world, ContactCallback{})`.
inline void clearContactCallback(IPhysicsBackend& backend,
                                 PhysicsWorldId world) {
    backend.setContactCallback(world, ContactCallback{});
}

} // namespace threadmaxx::physics
