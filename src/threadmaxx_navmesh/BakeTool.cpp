#include "threadmaxx_navmesh/bake.hpp"

#include "threadmaxx_navmesh/config.hpp"
#include "threadmaxx_navmesh/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace threadmaxx::navmesh {

namespace {

// ---------------------------------------------------------------------------
// Wire helpers — must match the format the registry reads:
//   [u32 magic 'NVMX'][u32 version=2]
//   [u32 nameLen][bytes name]
//   [u32 tileCount = 1]
//   [u32 tileId][u32 vertexCount][u32 polyCount][u32 indexCount]
//   [Vec3 * vertexCount]
//   [NavPoly * polyCount]
//   [u32  * indexCount]   ; vertexIndices
//   [u32  * indexCount]   ; neighborPolys
//   [u32 portalCount = 0]
// ---------------------------------------------------------------------------

void appendBytes(std::vector<std::byte>& out, const void* src, std::size_t n) {
    const auto offset = out.size();
    out.resize(offset + n);
    if (n != 0) std::memcpy(out.data() + offset, src, n);
}

template <typename T>
void writePod(std::vector<std::byte>& out, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    appendBytes(out, &v, sizeof(T));
}

void writeU32(std::vector<std::byte>& out, std::uint32_t v) {
    writePod(out, v);
}

void writeString(std::vector<std::byte>& out, const std::string& s) {
    writeU32(out, static_cast<std::uint32_t>(s.size()));
    appendBytes(out, s.data(), s.size());
}

template <typename T>
void writeVec(std::vector<std::byte>& out, const std::vector<T>& v) {
    appendBytes(out, v.data(), v.size() * sizeof(T));
}

// ---------------------------------------------------------------------------
// Edge key — undirected edge between two vertex ids. Sorted so that an
// edge appears with the same key from both incident polygons.
// ---------------------------------------------------------------------------

struct EdgeKey {
    std::uint32_t lo;
    std::uint32_t hi;
    bool operator==(const EdgeKey& other) const noexcept {
        return lo == other.lo && hi == other.hi;
    }
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& k) const noexcept {
        // FNV-1a-ish mix; the hash table is small per bake, no need for
        // a serious hash.
        std::uint64_t x = (static_cast<std::uint64_t>(k.lo) << 32) |
                          static_cast<std::uint64_t>(k.hi);
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return static_cast<std::size_t>(x);
    }
};

EdgeKey makeEdge(std::uint32_t a, std::uint32_t b) noexcept {
    return EdgeKey{std::min(a, b), std::max(a, b)};
}

/// One incidence of an edge: (polygon index, edge slot in that polygon).
struct EdgeIncidence {
    std::uint32_t poly;
    std::uint32_t edge;
};

float triangleArea2D(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    // XZ-projected signed area * 2. Triangles are walkable surfaces in
    // the v1.0 model; we treat them as 2D from above. A pure-vertical
    // triangle is degenerate by this rule, which matches the runtime's
    // even-odd locate that also assumes XZ projection.
    const float ux = b.x - a.x;
    const float uz = b.z - a.z;
    const float vx = c.x - a.x;
    const float vz = c.z - a.z;
    return ux * vz - uz * vx;
}

} // namespace

BakeResult bakeNavMesh(const BakeInput& input) {
    BakeResult result;

    if (input.vertices.empty() || input.triangles.empty()) {
        result.error = BakeError::EmptyInput;
        result.diagnostic = "bake input has zero vertices or zero triangles";
        return result;
    }

    const std::size_t polyCount = input.triangles.size();
    if (polyCount > kNavMeshMaxPolysPerTile) {
        result.error = BakeError::TooManyPolygons;
        result.diagnostic = "polygon count exceeds per-tile cap";
        return result;
    }

    const std::uint32_t vertexCount =
        static_cast<std::uint32_t>(input.vertices.size());

    // ----- Validate triangles + build edge incidence map ----------------
    std::unordered_map<EdgeKey, std::vector<EdgeIncidence>, EdgeKeyHash> edges;
    edges.reserve(polyCount * 3);

    for (std::size_t i = 0; i < polyCount; ++i) {
        const BakeInputTriangle& t = input.triangles[i];
        if (t.a >= vertexCount || t.b >= vertexCount || t.c >= vertexCount) {
            result.error = BakeError::InvalidIndex;
            result.diagnostic = "triangle indexes a vertex past the pool";
            return result;
        }
        if (t.a == t.b || t.b == t.c || t.a == t.c) {
            result.error = BakeError::DegenerateTriangle;
            result.diagnostic = "triangle has a repeated vertex id";
            return result;
        }
        const Vec3& va = input.vertices[t.a];
        const Vec3& vb = input.vertices[t.b];
        const Vec3& vc = input.vertices[t.c];
        const float doubleArea = triangleArea2D(va, vb, vc);
        if (std::fabs(doubleArea) <= 1e-6f) {
            result.error = BakeError::DegenerateTriangle;
            result.diagnostic =
                "triangle is degenerate (zero XZ-projected area)";
            return result;
        }
        // Three edges, in the order the runtime walks them. Edge `e`
        // runs vertex[indexStart + e] → vertex[indexStart + (e+1)%3].
        const std::uint32_t ids[3] = {t.a, t.b, t.c};
        for (std::uint32_t e = 0; e < 3; ++e) {
            const EdgeKey k = makeEdge(ids[e], ids[(e + 1) % 3]);
            auto& list = edges[k];
            list.push_back(EdgeIncidence{static_cast<std::uint32_t>(i), e});
            if (list.size() > 2) {
                result.error = BakeError::NonManifoldEdge;
                result.diagnostic =
                    "edge is shared by more than two triangles";
                return result;
            }
        }
    }

    // ----- Build tile pools --------------------------------------------
    std::vector<NavPoly> polys;
    polys.reserve(polyCount);
    std::vector<std::uint32_t> vertexIndices;
    vertexIndices.reserve(polyCount * 3);
    std::vector<std::uint32_t> neighborPolys;
    neighborPolys.reserve(polyCount * 3);

    for (std::size_t i = 0; i < polyCount; ++i) {
        const BakeInputTriangle& t = input.triangles[i];
        NavPoly p;
        p.indexStart = static_cast<std::uint32_t>(vertexIndices.size());
        p.indexCount = 3;
        p.areaTag = t.areaTag;
        polys.push_back(p);

        vertexIndices.push_back(t.a);
        vertexIndices.push_back(t.b);
        vertexIndices.push_back(t.c);

        // Neighbor lookup per edge.
        const std::uint32_t ids[3] = {t.a, t.b, t.c};
        for (std::uint32_t e = 0; e < 3; ++e) {
            const EdgeKey k = makeEdge(ids[e], ids[(e + 1) % 3]);
            const auto it = edges.find(k);
            std::uint32_t neighbor = kInvalidPolyIndex;
            if (it != edges.end()) {
                for (const EdgeIncidence& inc : it->second) {
                    if (inc.poly != static_cast<std::uint32_t>(i)) {
                        neighbor = inc.poly;
                        break;
                    }
                }
            }
            neighborPolys.push_back(neighbor);
        }
    }

    // ----- Emit v2 blob -------------------------------------------------
    std::vector<std::byte>& out = result.blob;
    out.reserve(64 + input.vertices.size() * sizeof(Vec3) +
                polys.size() * sizeof(NavPoly) +
                vertexIndices.size() * sizeof(std::uint32_t) * 2);

    writeU32(out, kNavMeshBlobMagic);
    writeU32(out, kNavMeshBlobVersion);
    writeString(out, input.name);
    writeU32(out, 1u);  // tile count

    writeU32(out, input.tileId);
    writeU32(out, vertexCount);
    writeU32(out, static_cast<std::uint32_t>(polys.size()));
    writeU32(out, static_cast<std::uint32_t>(vertexIndices.size()));

    // Vertex pool is the input pool verbatim.
    appendBytes(out, input.vertices.data(),
                input.vertices.size() * sizeof(Vec3));
    writeVec(out, polys);
    writeVec(out, vertexIndices);
    writeVec(out, neighborPolys);

    // No cross-tile portals — single-tile bake.
    writeU32(out, 0u);

    return result;
}

} // namespace threadmaxx::navmesh
