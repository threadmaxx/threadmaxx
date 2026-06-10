#pragma once

#include "threadmaxx_navmesh/types.hpp"

#include "threadmaxx/Components.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// Offline bake for `threadmaxx_navmesh`.
///
/// N9 ships a **triangle-direct** bake: every input triangle becomes a
/// single 3-vertex polygon in the output mesh. Adjacency is derived
/// from shared edges; per-triangle area tags are forwarded verbatim.
/// Voxelization + automatic walkability classification (Recast-style)
/// is intentionally out of scope for v1.0 — see `FUTURE_WORK.md`.
///
/// The bake lives in the same static library as the runtime; clients
/// that want a standalone tool can link `examples/navmesh_bake`. The
/// runtime never calls into the bake.
namespace threadmaxx::navmesh {

using ::threadmaxx::Vec3;

/// One input triangle. Vertex ids index into `BakeInput::vertices`;
/// winding is preserved (the runtime stores CCW polygons but the bake
/// does not enforce it — caller-supplied geometry is trusted).
struct BakeInputTriangle {
    std::uint32_t a{};
    std::uint32_t b{};
    std::uint32_t c{};
    /// Area tag forwarded into the baked polygon. Game-side semantics;
    /// the library treats it as an opaque 16-bit value.
    std::uint16_t areaTag{};
};

/// Bake input. One tile per call — multi-tile baking lands with the
/// v1.x tile-streaming work. The caller is responsible for splitting
/// the world into tile-sized chunks if needed.
struct BakeInput {
    /// Shared vertex pool. Polygons reference these by index.
    std::span<const Vec3> vertices;
    /// Triangle soup. Each triangle becomes one output polygon.
    std::span<const BakeInputTriangle> triangles;
    /// Asset name carried into the baked blob header. Empty is fine.
    std::string name;
    /// Tile id stamped on the single output tile.
    NavTileId tileId{0};
};

/// Diagnostic reason a `bakeNavMesh()` call failed. Use
/// `BakeResult::error` to discriminate; `BakeResult::diagnostic`
/// carries a human-readable string suitable for `std::cerr`.
enum class BakeError : std::uint8_t {
    None,                ///< Bake succeeded; blob is valid.
    EmptyInput,          ///< Zero vertices OR zero triangles.
    InvalidIndex,        ///< A triangle indexed past the vertex pool.
    DegenerateTriangle,  ///< Repeated vertex id OR zero-area triangle.
    NonManifoldEdge,     ///< An edge is shared by more than two
                         ///< polygons (impossible on a clean 2-manifold
                         ///< walkable surface).
    TooManyPolygons,     ///< Polygon count would exceed the per-tile
                         ///< cap (`kNavMeshMaxPolysPerTile`).
};

/// Bake output. On success `error == None` and `blob` is a v2 navmesh
/// asset ready for `NavMeshRegistry::load`. On failure `blob` is empty
/// and `diagnostic` carries a short explanation.
struct BakeResult {
    std::vector<std::byte> blob;
    BakeError error{BakeError::None};
    std::string diagnostic;
};

/// Pure function. Allocates only inside the returned `BakeResult`; the
/// caller owns the resulting blob bytes.
///
/// Validates the input, builds polygon adjacency by matching shared
/// edges, and emits a v2 blob. Open edges (a triangle edge shared by
/// no other triangle) are stamped `kInvalidPolyIndex` in the
/// neighbor table — those become walkable boundaries at runtime.
///
/// @thread_safety Thread-safe; allocates no global state.
[[nodiscard]] BakeResult bakeNavMesh(const BakeInput& input);

} // namespace threadmaxx::navmesh
