#pragma once

// Tiny baked-blob builder used by the navmesh registry tests. Mirrors
// the v2 wire format that `NavMeshRegistry::load()` consumes:
//
//   [u32 magic 'NVMX'][u32 version=2]
//   [u32 nameLen][bytes name]
//   [u32 tileCount]
//   per tile:
//     [u32 id][u32 vertexCount][u32 polyCount][u32 indexCount]
//     [Vec3 * vertexCount]
//     [NavPoly * polyCount]
//     [u32  * indexCount]   ; vertexIndices
//     [u32  * indexCount]   ; neighborPolys
//   [u32 portalCount]
//   [NavPortal * portalCount]
//
// `make16PolyFlatSquare()` produces the canonical N1 fixture: a flat
// 4x4 grid of quads (16 polygons) tiled into a single tile. The grid
// occupies [0,4] x [0,0] x [0,4] in world space.
//
// `make4TileLShape()` produces the N2 fixture: four 2x2-quad tiles
// arranged in an L (T0 bottom-left, T1 bottom-middle, T2 above T0, T3
// bottom-right) with 6 cross-tile portals stitching the inner edges.

#include "threadmaxx_navmesh/config.hpp"
#include "threadmaxx_navmesh/mesh.hpp"

#include "threadmaxx/Components.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace threadmaxx::navmesh::test_fixtures {

class BlobBuilder {
public:
    template <typename T>
    void writePod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto offset = bytes_.size();
        bytes_.resize(offset + sizeof(T));
        std::memcpy(bytes_.data() + offset, &v, sizeof(T));
    }

    void writeU32(std::uint32_t v) { writePod(v); }

    void writeString(const std::string& s) {
        writeU32(static_cast<std::uint32_t>(s.size()));
        const auto offset = bytes_.size();
        bytes_.resize(offset + s.size());
        std::memcpy(bytes_.data() + offset, s.data(), s.size());
    }

    template <typename T>
    void writeVec(const std::vector<T>& v) {
        const std::size_t bytes = v.size() * sizeof(T);
        const auto offset = bytes_.size();
        bytes_.resize(offset + bytes);
        if (bytes) std::memcpy(bytes_.data() + offset, v.data(), bytes);
    }

    std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes_.data()), bytes_.size());
    }

    /// Truncate the blob to `n` bytes — used by the "corrupted blob"
    /// test to verify the registry catches short reads cleanly.
    void truncateTo(std::size_t n) noexcept {
        if (n < bytes_.size()) bytes_.resize(n);
    }

private:
    std::vector<unsigned char> bytes_;
};

/// Canonical N1 fixture: one tile, 16 quads on a flat 4x4 grid at y=0.
inline std::vector<unsigned char> make16PolyFlatSquare(
    std::uint32_t magic   = kNavMeshBlobMagic,
    std::uint32_t version = kNavMeshBlobVersion,
    const std::string& name = "flat_square_16") {

    BlobBuilder b;
    b.writeU32(magic);
    b.writeU32(version);
    b.writeString(name);
    b.writeU32(1);  // tile count

    // One tile.
    const std::uint32_t tileId = 0;
    // 5x5 grid of vertices => 25 verts, 4x4 = 16 quad polys.
    std::vector<Vec3> verts;
    verts.reserve(25);
    for (int z = 0; z < 5; ++z) {
        for (int x = 0; x < 5; ++x) {
            verts.push_back(Vec3{static_cast<float>(x), 0.0f,
                                 static_cast<float>(z)});
        }
    }
    // Each quad uses 4 vertex indices (CCW from above).
    std::vector<NavPoly> polys;
    std::vector<std::uint32_t> idx;
    std::vector<std::uint32_t> neighbors;
    polys.reserve(16);
    idx.reserve(64);
    neighbors.reserve(64);
    auto vid = [](int x, int z) {
        return static_cast<std::uint32_t>(z * 5 + x);
    };
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            NavPoly p;
            p.indexStart = static_cast<std::uint32_t>(idx.size());
            p.indexCount = 4;
            p.areaTag = 0;  // ground
            polys.push_back(p);

            idx.push_back(vid(x,     z    ));
            idx.push_back(vid(x + 1, z    ));
            idx.push_back(vid(x + 1, z + 1));
            idx.push_back(vid(x,     z + 1));

            // Neighbor poly per edge, ordered to match the verts above:
            // edge[0] = (x,z)->(x+1,z) is the -z edge (neighbor at z-1)
            // edge[1] = (x+1,z)->(x+1,z+1) is the +x edge (neighbor at x+1)
            // edge[2] = (x+1,z+1)->(x,z+1) is the +z edge (neighbor at z+1)
            // edge[3] = (x,z+1)->(x,z) is the -x edge (neighbor at x-1)
            auto polyAt = [](int xx, int zz) {
                if (xx < 0 || xx >= 4 || zz < 0 || zz >= 4)
                    return kInvalidPolyIndex;
                return static_cast<std::uint32_t>(zz * 4 + xx);
            };
            neighbors.push_back(polyAt(x,     z - 1));
            neighbors.push_back(polyAt(x + 1, z    ));
            neighbors.push_back(polyAt(x,     z + 1));
            neighbors.push_back(polyAt(x - 1, z    ));
        }
    }

    b.writeU32(tileId);
    b.writeU32(static_cast<std::uint32_t>(verts.size()));
    b.writeU32(static_cast<std::uint32_t>(polys.size()));
    b.writeU32(static_cast<std::uint32_t>(idx.size()));
    b.writeVec(verts);
    b.writeVec(polys);
    b.writeVec(idx);
    b.writeVec(neighbors);

    // v2: empty portal table — the single tile has no cross-tile edges.
    b.writeU32(0);

    auto view = b.view();
    return std::vector<unsigned char>(
        reinterpret_cast<const unsigned char*>(view.data()),
        reinterpret_cast<const unsigned char*>(view.data()) + view.size());
}

/// N2 fixture: an L-shape of four 2x2-quad tiles. Tile layout (looking
/// down, +x to the right, +z up the page):
///
///   z=4 +-----+
///       | T2  |
///   z=2 +-----+-----+-----+
///       | T0  | T1  | T3  |
///   z=0 +-----+-----+-----+
///       x=0   x=2   x=4   x=6
///
/// Each tile carries 4 quads on a 3x3 vertex grid (9 verts, polygon ids
/// `0..3` ordered as (0,0), (1,0), (0,1), (1,1)). Six portals stitch
/// the inner edges:
///   T0:(1,0)+x ↔ T1:(0,0)-x
///   T0:(1,1)+x ↔ T1:(0,1)-x
///   T0:(0,1)+z ↔ T2:(0,0)-z
///   T0:(1,1)+z ↔ T2:(1,0)-z
///   T1:(1,0)+x ↔ T3:(0,0)-x
///   T1:(1,1)+x ↔ T3:(0,1)-x
inline std::vector<unsigned char> make4TileLShape(
    std::uint32_t magic   = kNavMeshBlobMagic,
    std::uint32_t version = kNavMeshBlobVersion,
    const std::string& name = "l_shape_4_tiles") {

    BlobBuilder b;
    b.writeU32(magic);
    b.writeU32(version);
    b.writeString(name);
    b.writeU32(4);  // tile count

    // Build a 2x2-quad tile whose lower-left corner sits at (originX,
    // originZ). Returns the per-tile {vertices, polys, vertexIndices,
    // neighborPolys} pools, with intra-tile neighbors wired the same way
    // the N1 fixture does (kInvalidPolyIndex for tile-boundary edges).
    auto writeTile = [&](NavTileId tileId, float originX, float originZ) {
        std::vector<Vec3> verts;
        verts.reserve(9);
        for (int z = 0; z < 3; ++z) {
            for (int x = 0; x < 3; ++x) {
                verts.push_back(Vec3{
                    originX + static_cast<float>(x), 0.0f,
                    originZ + static_cast<float>(z)});
            }
        }
        std::vector<NavPoly> polys;
        std::vector<std::uint32_t> idx;
        std::vector<std::uint32_t> neighbors;
        polys.reserve(4);
        idx.reserve(16);
        neighbors.reserve(16);
        auto vid = [](int x, int z) {
            return static_cast<std::uint32_t>(z * 3 + x);
        };
        auto polyAt = [](int xx, int zz) {
            if (xx < 0 || xx >= 2 || zz < 0 || zz >= 2)
                return kInvalidPolyIndex;
            return static_cast<std::uint32_t>(zz * 2 + xx);
        };
        for (int z = 0; z < 2; ++z) {
            for (int x = 0; x < 2; ++x) {
                NavPoly p;
                p.indexStart = static_cast<std::uint32_t>(idx.size());
                p.indexCount = 4;
                p.areaTag = 0;
                polys.push_back(p);

                idx.push_back(vid(x,     z    ));
                idx.push_back(vid(x + 1, z    ));
                idx.push_back(vid(x + 1, z + 1));
                idx.push_back(vid(x,     z + 1));

                neighbors.push_back(polyAt(x,     z - 1));
                neighbors.push_back(polyAt(x + 1, z    ));
                neighbors.push_back(polyAt(x,     z + 1));
                neighbors.push_back(polyAt(x - 1, z    ));
            }
        }

        b.writeU32(tileId);
        b.writeU32(static_cast<std::uint32_t>(verts.size()));
        b.writeU32(static_cast<std::uint32_t>(polys.size()));
        b.writeU32(static_cast<std::uint32_t>(idx.size()));
        b.writeVec(verts);
        b.writeVec(polys);
        b.writeVec(idx);
        b.writeVec(neighbors);
    };

    writeTile(/*tileId=*/0, /*originX=*/0.0f, /*originZ=*/0.0f);
    writeTile(/*tileId=*/1, /*originX=*/2.0f, /*originZ=*/0.0f);
    writeTile(/*tileId=*/2, /*originX=*/0.0f, /*originZ=*/2.0f);
    writeTile(/*tileId=*/3, /*originX=*/4.0f, /*originZ=*/0.0f);

    // Edge convention (matches writeTile above):
    //   edge 0 = -z, edge 1 = +x, edge 2 = +z, edge 3 = -x.
    // Poly ids within a 2x2 tile: polyAt(x,z) = z*2 + x →
    //   (0,0)=0, (1,0)=1, (0,1)=2, (1,1)=3.
    constexpr std::uint32_t kEdgeNZ = 0;
    constexpr std::uint32_t kEdgePX = 1;
    constexpr std::uint32_t kEdgePZ = 2;
    constexpr std::uint32_t kEdgeNX = 3;

    std::vector<NavPortal> portals;
    portals.reserve(6);
    auto pushPortal = [&](NavTileId tA, NavPolyId pA, std::uint32_t eA,
                          NavTileId tB, NavPolyId pB, std::uint32_t eB) {
        portals.push_back(NavPortal{tA, pA, eA, tB, pB, eB});
    };

    // T0 ↔ T1 (shared x=2).
    pushPortal(0, /*polyAt(1,0)=*/1, kEdgePX, 1, /*polyAt(0,0)=*/0, kEdgeNX);
    pushPortal(0, /*polyAt(1,1)=*/3, kEdgePX, 1, /*polyAt(0,1)=*/2, kEdgeNX);
    // T0 ↔ T2 (shared z=2).
    pushPortal(0, /*polyAt(0,1)=*/2, kEdgePZ, 2, /*polyAt(0,0)=*/0, kEdgeNZ);
    pushPortal(0, /*polyAt(1,1)=*/3, kEdgePZ, 2, /*polyAt(1,0)=*/1, kEdgeNZ);
    // T1 ↔ T3 (shared x=4).
    pushPortal(1, /*polyAt(1,0)=*/1, kEdgePX, 3, /*polyAt(0,0)=*/0, kEdgeNX);
    pushPortal(1, /*polyAt(1,1)=*/3, kEdgePX, 3, /*polyAt(0,1)=*/2, kEdgeNX);

    b.writeU32(static_cast<std::uint32_t>(portals.size()));
    b.writeVec(portals);

    auto view = b.view();
    return std::vector<unsigned char>(
        reinterpret_cast<const unsigned char*>(view.data()),
        reinterpret_cast<const unsigned char*>(view.data()) + view.size());
}

/// Convert the unsigned-char vec back into a byte span the registry
/// will accept. Lives next to `make16PolyFlatSquare` so tests don't
/// have to cast.
inline std::span<const std::byte> bytes(const std::vector<unsigned char>& v) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(v.data()), v.size());
}

} // namespace threadmaxx::navmesh::test_fixtures
