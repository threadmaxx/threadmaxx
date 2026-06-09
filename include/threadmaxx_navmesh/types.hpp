#pragma once

#include <cstdint>

/// Opaque identifier PODs for the threadmaxx_navmesh library.
///
/// All ids are trivially-copyable handles into the registry's internal
/// tables. The library never inspects the bit layout. A zero-valued id
/// is reserved as "invalid" and must be returned by failing
/// `load` / `request` calls.
namespace threadmaxx::navmesh {

/// Identifies a loaded navmesh asset inside a `NavMeshRegistry`.
using NavMeshId = std::uint64_t;

/// Per-tile identifier scoped to a single navmesh. Sized for streamed
/// worlds (16k+ tiles is fine).
using NavTileId = std::uint32_t;

/// Per-polygon identifier scoped to a single tile. The bake assigns
/// polygons in input order.
using NavPolyId = std::uint32_t;

/// Identifies an outstanding (or completed) path request inside a
/// `PathQueryService`. Reserved for batch N3; declared here so the
/// type lives next to its siblings.
using PathId = std::uint64_t;

/// Identifies an in-flight navmesh agent inside a future agent
/// registry. Reserved for batch N7; declared here so the type lives
/// next to its siblings.
using NavAgentId = std::uint64_t;

/// Refcounted handle into the `NavMeshRegistry`. Carries a generation
/// counter so reused id slots can't be confused with the prior tenant.
struct NavMeshRef {
    NavMeshId id{};
    std::uint32_t generation{};

    constexpr bool operator==(const NavMeshRef&) const noexcept = default;
    constexpr explicit operator bool() const noexcept { return id != 0; }
};

} // namespace threadmaxx::navmesh
