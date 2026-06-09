#pragma once

#include "threadmaxx_navmesh/types.hpp"

#include "threadmaxx/Components.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Runtime navmesh representation + the registry that owns it.
///
/// N1 ships the load / unload / valid / meta surface. N2 layers
/// adjacency walking on top of the same tile model; N3 layers A* on
/// top of the polygon graph.
namespace threadmaxx::navmesh {

using ::threadmaxx::Vec3;

/// Polygon header inside a tile. Each polygon owns a contiguous slice
/// of the tile's `vertexIndices` (`[indexStart, indexStart + indexCount)`)
/// and a slice of the same length in `neighborPolys` (one neighbor
/// poly id per edge — `kInvalidPolyIndex` for border edges).
struct NavPoly {
    /// Index of the first vertex this polygon uses inside the tile's
    /// `vertexIndices` array.
    std::uint32_t indexStart{};

    /// Number of vertices this polygon uses. Polygons are convex and
    /// typically have 3-6 vertices.
    std::uint16_t indexCount{};

    /// Area tag (e.g. ground, water, jump-pad). Game-side semantics;
    /// the library passes it through unchanged.
    std::uint16_t areaTag{};
};

/// Sentinel returned by `neighborPolys` for border edges (no neighbor
/// across that edge inside the same tile).
inline constexpr std::uint32_t kInvalidPolyIndex = 0xFFFFFFFFu;

/// One tile in the runtime navmesh. Tiles own their geometry; cross-
/// tile adjacency lives at the mesh level (N2 work).
struct NavTile {
    NavTileId id{};

    /// Vertex pool the tile's polygons index into. Per-tile, not
    /// shared across tiles — keeps streaming local.
    std::vector<Vec3> vertices;

    /// Polygon headers.
    std::vector<NavPoly> polygons;

    /// Vertex index pool — `polygon.indexStart..+indexCount` slices in.
    std::vector<std::uint32_t> vertexIndices;

    /// Per-edge neighbor polygon (parallel slice to `vertexIndices`).
    /// Border edges read `kInvalidPolyIndex`.
    std::vector<std::uint32_t> neighborPolys;

    /// Axis-aligned bounding box of the tile (min, max). Used by query
    /// broadphase culling in N3+. Computed once at load time.
    Vec3 aabbMin{};
    Vec3 aabbMax{};
};

/// User-visible metadata about a loaded navmesh. Returned by
/// `NavMeshRegistry::meta`.
struct NavMeshMeta {
    std::string name;
    std::uint32_t tileCount{};
    std::uint32_t polygonCount{};
    std::uint32_t vertexCount{};
};

/// Runtime navmesh asset. Internally owned by `NavMeshRegistry`; the
/// public surface only exposes `const NavMesh*` accessors.
class NavMesh {
public:
    NavMesh() = default;
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;
    NavMesh(NavMesh&&) noexcept = default;
    NavMesh& operator=(NavMesh&&) noexcept = default;

    /// Tile span — N1 exposes this for tests and the future query path;
    /// the bake produces tiles in load order so the index in this span
    /// matches the tile's load-time position.
    std::span<const NavTile> tiles() const noexcept { return tiles_; }

    /// Lookup a tile by id. Returns `nullptr` if no tile carries `id`.
    const NavTile* findTile(NavTileId id) const noexcept;

    /// Sum of `polygons.size()` across every tile — cached at load
    /// time so meta() is O(1).
    std::uint32_t polygonCount() const noexcept { return polygonCount_; }

    /// Sum of `vertices.size()` across every tile — cached at load
    /// time so meta() is O(1).
    std::uint32_t vertexCount() const noexcept { return vertexCount_; }

    /// Asset name carried in the blob header.
    const std::string& name() const noexcept { return name_; }

private:
    friend class NavMeshRegistry;

    std::string name_;
    std::vector<NavTile> tiles_;
    std::uint32_t polygonCount_{};
    std::uint32_t vertexCount_{};
};

/// Diagnostic reason a `load()` call failed. The enum value is
/// returned through `lastLoadError()` so tests + tools can probe the
/// failure mode without parsing strings.
enum class NavMeshLoadError : std::uint8_t {
    None,            ///< Most recent load succeeded.
    EmptyBlob,       ///< Input span had zero bytes.
    Truncated,       ///< Header / payload runs past the end of the blob.
    InvalidMagic,    ///< First 4 bytes don't match `kNavMeshBlobMagic`.
    UnsupportedVersion, ///< `version` doesn't match `kNavMeshBlobVersion`.
    InvalidTileCount,   ///< `tileCount` exceeds `kNavMeshMaxTiles`.
    InvalidPolyCount,   ///< A tile's polygon count exceeds
                        ///< `kNavMeshMaxPolysPerTile`.
    InvalidIndex,    ///< A polygon points past the tile's vertex pool.
};

/// Owns every loaded navmesh asset. N1 only does `load` / `unload`;
/// later batches add the query side (N3) on top of the same registry
/// instance.
///
/// Thread-safe across `load` / `unload` / `find` calls; the internal
/// vector is mutex-protected. Read accessors that return `const
/// NavMesh*` are safe to dereference for as long as the caller holds a
/// matching valid `NavMeshRef` (refs survive across unrelated unloads).
class NavMeshRegistry {
public:
    NavMeshRegistry();
    ~NavMeshRegistry();
    NavMeshRegistry(const NavMeshRegistry&) = delete;
    NavMeshRegistry& operator=(const NavMeshRegistry&) = delete;
    NavMeshRegistry(NavMeshRegistry&&) noexcept;
    NavMeshRegistry& operator=(NavMeshRegistry&&) noexcept;

    /// Parse `bakedData` and install the resulting asset in the
    /// registry. Returns a valid `NavMeshRef` on success or an invalid
    /// one (`!ref` is true) on failure; on failure `lastLoadError()`
    /// reports the reason.
    NavMeshRef load(std::span<const std::byte> bakedData);

    /// Release the asset identified by `ref`. Subsequent `isValid(ref)`
    /// returns false. The id slot is recycled with a fresh generation
    /// on the next load. No-op if `ref` is already invalid or stale.
    void unload(NavMeshRef ref);

    /// Returns true if `ref` still points at the live asset (matching
    /// id AND generation).
    bool isValid(NavMeshRef ref) const noexcept;

    /// Metadata snapshot for `ref`. `std::nullopt` if `ref` is stale.
    std::optional<NavMeshMeta> meta(NavMeshRef ref) const;

    /// Pointer to the live asset, or `nullptr` if `ref` is stale. The
    /// returned pointer is valid until the asset is unloaded.
    const NavMesh* find(NavMeshRef ref) const noexcept;

    /// Number of currently-loaded assets. Useful for tests / HUD.
    std::size_t size() const noexcept;

    /// Reason the most recent `load` call failed, or
    /// `NavMeshLoadError::None` if it succeeded. Per-thread state —
    /// only meaningful in single-thread test code.
    NavMeshLoadError lastLoadError() const noexcept { return lastError_; }

private:
    struct Slot {
        std::uint32_t generation{};
        std::unique_ptr<NavMesh> mesh;
    };

    NavMeshLoadError lastError_{NavMeshLoadError::None};
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::navmesh
