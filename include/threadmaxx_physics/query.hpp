#pragma once

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/body.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

/// Synchronous spatial queries against a `PhysicsWorldId`.
///
/// Three flavors:
///
/// - **Raycast** — closest-hit ray-vs-body. Returns the entry point on
///   the body's collision volume, the surface normal at that point, and
///   the parametric distance along the ray.
/// - **Sweep** — closest-hit sphere-cast (a moving sphere of `radius`
///   from `start` along `direction`). Equivalent to a raycast against
///   bodies inflated by `radius` (Minkowski sum). Useful for "where
///   would my character bump into the world" gameplay queries.
/// - **Overlap** — every body whose collision volume intersects a sphere
///   at `center` of `radius`. Returns a (possibly empty) list of
///   `BodyId`s; no ordering guarantee.
///
/// **Layer filtering.** Every request carries a 32-bit `layerMask`. A
/// body is considered iff `(1u << body.layer) & request.layerMask != 0`.
/// The default mask `0xFFFFFFFF` matches every layer.
///
/// **Stub backend semantics.** The `StubBackend` answers queries against
/// each body's world-space AABB — the union of every attached shape's
/// local-space AABB, translated by the body's current position.
/// Rotation is ignored at the stub (no true OBB narrowphase); real
/// backends (P9 Jolt) do the proper SAT / narrowphase. Bodies with no
/// shapes degenerate to a zero-extent AABB at `body.state.position`,
/// which still answers overlaps and raycasts at the point itself.
///
/// All helpers are sim-thread-only by convention — they call into the
/// backend directly and inherit its threading contract (see
/// `IPhysicsBackend` doc).
namespace threadmaxx::physics {

/// Closest-hit ray query input.
///
/// `direction` is assumed unit-length; backends MAY divide by its
/// magnitude internally but the Stub does not (caller-side
/// responsibility). `maxDistance` clamps the search interval; a hit
/// past `maxDistance` is reported as no-hit.
struct RaycastRequest {
    Vec3 origin{};
    Vec3 direction{1.0f, 0.0f, 0.0f};
    float maxDistance{1.0e6f};
    std::uint32_t layerMask{0xFFFFFFFFu};
};

/// Closest-hit ray result. `position = origin + direction * distance`;
/// `normal` points back toward the half-space the ray came from (so
/// `dot(normal, -direction) > 0` at any concave-free hit).
struct RaycastHit {
    BodyId body{};
    Vec3 position{};
    Vec3 normal{};
    float distance{0.0f};
};

/// Closest-hit sphere-cast input. The sweep volume is a sphere of
/// `radius` centered at `start`, translated along `direction` for at
/// most `maxDistance` units. Backends treat this as a raycast against
/// each body's collision volume inflated by `radius` (Minkowski sum).
struct SweepRequest {
    float radius{0.5f};
    Vec3 start{};
    Vec3 direction{1.0f, 0.0f, 0.0f};
    float maxDistance{1.0e6f};
    std::uint32_t layerMask{0xFFFFFFFFu};
};

/// Closest-hit sphere-cast result. `position` is the center of the
/// swept sphere at first contact (NOT the contact point on the body).
struct SweepHit {
    BodyId body{};
    Vec3 position{};
    Vec3 normal{};
    float distance{0.0f};
};

/// Overlap query input. `radius == 0` is the point-overlap form (which
/// body, if any, contains `center`).
struct OverlapRequest {
    Vec3 center{};
    float radius{0.0f};
    std::uint32_t layerMask{0xFFFFFFFFu};
};

/// Cast a closest-hit ray into `world`. Thin pass-through to
/// `IPhysicsBackend::raycast`.
inline std::optional<RaycastHit> raycast(IPhysicsBackend& backend,
                                         PhysicsWorldId world,
                                         const RaycastRequest& request) {
    return backend.raycast(world, request);
}

/// Cast a closest-hit sphere along `request.direction`. Thin
/// pass-through to `IPhysicsBackend::sweep`.
inline std::optional<SweepHit> sweep(IPhysicsBackend& backend,
                                     PhysicsWorldId world,
                                     const SweepRequest& request) {
    return backend.sweep(world, request);
}

/// Reuse-the-buffer overlap form. `outBodies` is cleared before the
/// call; the backend appends every hit body. Game code that drives
/// overlap queries per-tick should reuse a system-owned vector here to
/// avoid per-frame allocations.
inline void overlapBodies(IPhysicsBackend& backend,
                          PhysicsWorldId world,
                          const OverlapRequest& request,
                          std::vector<BodyId>& outBodies) {
    backend.overlap(world, request, outBodies);
}

/// Convenience form that allocates a fresh `std::vector<BodyId>` and
/// returns it by value. Prefer the buffer-reuse overload in hot paths.
inline std::vector<BodyId> overlapBodies(IPhysicsBackend& backend,
                                         PhysicsWorldId world,
                                         const OverlapRequest& request) {
    std::vector<BodyId> out;
    backend.overlap(world, request, out);
    return out;
}

} // namespace threadmaxx::physics
