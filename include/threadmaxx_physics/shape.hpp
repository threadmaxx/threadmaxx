#pragma once

#include "threadmaxx/Components.hpp"

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
/// - **Compound**   — composed of multiple primitives; the P2 batch
///   adds the composition API.
struct ShapeDesc {
    ShapeType type{ShapeType::Box};

    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius{0.5f};
    float height{1.0f};

    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices;
};

} // namespace threadmaxx::physics
