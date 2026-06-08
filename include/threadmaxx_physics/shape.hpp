#pragma once

#include "threadmaxx/Components.hpp"
#include "threadmaxx_physics/types.hpp"

#include <cstdint>
#include <vector>

/// Collider shape descriptors.
///
/// Shapes are registered once with the backend and shared by reference
/// across bodies. The `ShapeDesc` value type is intentionally generous
/// (carries vectors for mesh data) — backends are expected to cook /
/// validate at `createShape` time and discard the source.
namespace threadmaxx::physics {

using ::threadmaxx::Vec3;

/// Collider geometry kind. P1 backends are required to accept all
/// variants in the API; only Box / Sphere / Capsule have meaningful
/// stub-backend behavior in P5 queries. Mesh / ConvexHull / Compound
/// are placeholder until P9 wires real backends.
enum class ShapeType : std::uint8_t {
    Box        = 0,
    Sphere     = 1,
    Capsule    = 2,
    ConvexHull = 3,
    Mesh       = 4,
    Compound   = 5,
};

/// Shape blueprint. Only the fields relevant to `type` are read by the
/// backend — others may be left default-initialized.
///
/// Field interpretation by `ShapeType`:
/// - **Box**        — `halfExtents` (xyz half-widths in local space).
/// - **Sphere**     — `radius`.
/// - **Capsule**    — `radius` + `height` (cylinder length between caps,
///   oriented along local Y).
/// - **ConvexHull** — `vertices` (point cloud; backend computes the hull).
/// - **Mesh**       — `vertices` + `indices` (triangle list).
/// - **Compound**   — `children`: every entry is a previously-registered
///   ShapeId; the backend increments each child's refcount on
///   `createShape` so the parent keeps them alive. P2 ships
///   union-of-children AABBs at the origin; per-child local transforms
///   are deferred to a later batch (real backends need them for
///   solver-level composition; tests don't yet exercise it).
struct ShapeDesc {
    ShapeType type{ShapeType::Box};

    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius{0.5f};
    float height{1.0f};

    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<ShapeId> children;
};

/// Local-space axis-aligned bounding box returned by
/// `IPhysicsBackend::getShapeAabb`. Coordinates are in the shape's own
/// frame — bodies apply their world transform on top.
struct ShapeAabb {
    Vec3 min{};
    Vec3 max{};
};

} // namespace threadmaxx::physics
