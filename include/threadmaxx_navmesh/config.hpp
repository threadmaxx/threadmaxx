#pragma once

#include <cstdint>

/// Bake- and runtime-time configuration knobs for `threadmaxx_navmesh`.
///
/// N1 only defines the fields that the registry needs (binary-format
/// magic + version + per-load validation tolerances). Later batches
/// extend `NavMeshConfig` with bake parameters (N9) and query
/// tolerances (N3 — A* heuristic weight, etc.).
namespace threadmaxx::navmesh {

/// Four-byte magic prefix on every baked navmesh blob. Reads as 'NVMX'
/// in little-endian byte order; the registry rejects any other value
/// with an "invalid blob" diagnostic.
inline constexpr std::uint32_t kNavMeshBlobMagic = 0x584D564Eu;

/// Binary format version. Bumped any time the on-disk layout changes
/// in a way that breaks consumer-side load. v1 blobs from a future
/// engine release are NOT loaded by an older runtime.
inline constexpr std::uint32_t kNavMeshBlobVersion = 1u;

/// Maximum supported tile count per navmesh asset. Bounds the
/// registry's per-load allocation cost; a load that claims more tiles
/// than this is rejected as corrupted.
inline constexpr std::uint32_t kNavMeshMaxTiles = 65536u;

/// Maximum supported polygon count per tile. Bounds the per-tile
/// allocation; a tile header claiming more than this is rejected.
inline constexpr std::uint32_t kNavMeshMaxPolysPerTile = 65535u;

/// Library-wide tolerances. None of the fields apply yet (N1 only
/// loads — no queries), but the type lives next to the format
/// constants so N3 can flesh it out.
struct NavMeshConfig {
    /// Epsilon used by N3's A* tie-breaking. Default chosen to match
    /// the engine's stitched-view float comparison budget.
    float epsilon{1e-4f};
};

} // namespace threadmaxx::navmesh
