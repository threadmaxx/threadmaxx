#include "threadmaxx_assets/loaders/obj.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "threadmaxx_assets/config.hpp"
#include "threadmaxx_assets/detail/io.hpp"

namespace threadmaxx::assets {

namespace {

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

void normalize(Vec3& v) noexcept {
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= 0.0f) {
        return;
    }
    const float inv = 1.0f / std::sqrt(len2);
    v.x *= inv;
    v.y *= inv;
    v.z *= inv;
}

Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 n{ab.y * ac.z - ab.z * ac.y,
           ab.z * ac.x - ab.x * ac.z,
           ab.x * ac.y - ab.y * ac.x};
    normalize(n);
    return n;
}

// String view utilities --------------------------------------------------

bool isSpace(char c) noexcept { return c == ' ' || c == '\t'; }
bool isLineBreak(char c) noexcept { return c == '\n' || c == '\r'; }

std::string_view trimLeft(std::string_view s) noexcept {
    while (!s.empty() && isSpace(s.front())) {
        s.remove_prefix(1);
    }
    return s;
}

std::string_view nextToken(std::string_view& s) noexcept {
    s = trimLeft(s);
    const auto begin = s.data();
    while (!s.empty() && !isSpace(s.front()) && !isLineBreak(s.front())) {
        s.remove_prefix(1);
    }
    return {begin, static_cast<std::size_t>(s.data() - begin)};
}

float parseFloat(std::string_view tok) noexcept {
    if (tok.empty()) return 0.0f;
    char buf[64];
    const std::size_t n = tok.size() < sizeof(buf) - 1 ? tok.size() : sizeof(buf) - 1;
    for (std::size_t i = 0; i < n; ++i) {
        buf[i] = tok[i];
    }
    buf[n] = '\0';
    return std::strtof(buf, nullptr);
}

// Parses an OBJ face vertex token: a, a/b, a/b/c, or a//c.
// 1-based indices; negative indices (relative) are NOT supported in v1.0.
struct FaceVertex {
    std::int32_t v{};
    std::int32_t vt{};
    std::int32_t vn{};
};

FaceVertex parseFaceVertex(std::string_view tok) noexcept {
    FaceVertex fv{};
    std::int32_t* targets[3] = {&fv.v, &fv.vt, &fv.vn};
    int slot = 0;
    std::size_t i = 0;
    while (i < tok.size() && slot < 3) {
        if (tok[i] == '/') {
            ++slot;
            ++i;
            continue;
        }
        std::int32_t sign = 1;
        if (tok[i] == '-') {
            sign = -1;
            ++i;
        }
        std::int32_t v = 0;
        while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
            v = v * 10 + (tok[i] - '0');
            ++i;
        }
        if (v != 0) {
            *targets[slot] = sign * v;
        }
    }
    return fv;
}

// Lookup key for the (pos, normal, uv) → emitted vertex dedup map.
struct VertexKey {
    std::int32_t p, n, t;
    bool operator==(const VertexKey& o) const noexcept {
        return p == o.p && n == o.n && t == o.t;
    }
};

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& k) const noexcept {
        // Mix the three 32-bit indices via FNV-1a-64.
        std::uint64_t h = 0xcbf29ce484222325ull;
        auto mix = [&](std::int32_t v) {
            const auto* b = reinterpret_cast<const unsigned char*>(&v);
            for (int i = 0; i < 4; ++i) {
                h ^= b[i];
                h *= 0x100000001b3ull;
            }
        };
        mix(k.p); mix(k.n); mix(k.t);
        return static_cast<std::size_t>(h);
    }
};

AssetResult<MeshData> parseObjBytes(std::span<const std::byte> bytes,
                                    std::string_view sourcePath) {
    MeshData out;
    out.sourcePath = std::string(sourcePath);

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    positions.reserve(kInitialMeshVertexCapacity);
    normals.reserve(kInitialMeshVertexCapacity);
    uvs.reserve(kInitialMeshVertexCapacity);

    // Submesh tracking: at least one default group always exists.
    out.submeshes.push_back(MeshSubmesh{0, 0, kInvalidAssetId, ""});

    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> dedup;
    dedup.reserve(kInitialMeshVertexCapacity * 2);

    std::vector<FaceVertex> face;
    face.reserve(8);

    std::string_view src{reinterpret_cast<const char*>(bytes.data()), bytes.size()};

    auto emitFace = [&](std::span<const FaceVertex> verts) {
        if (verts.size() < 3) {
            return;
        }
        const std::int32_t nP = static_cast<std::int32_t>(positions.size());
        const std::int32_t nN = static_cast<std::int32_t>(normals.size());
        const std::int32_t nT = static_cast<std::int32_t>(uvs.size());

        auto emitVertex = [&](FaceVertex fv) -> std::uint32_t {
            // OBJ is 1-based; convert.
            std::int32_t p = fv.v > 0 ? fv.v - 1 : fv.v + nP;
            std::int32_t n = fv.vn > 0 ? fv.vn - 1 : (fv.vn == 0 ? -1 : fv.vn + nN);
            std::int32_t t = fv.vt > 0 ? fv.vt - 1 : (fv.vt == 0 ? -1 : fv.vt + nT);
            if (p < 0 || p >= nP) {
                p = 0;
            }
            VertexKey k{p, n, t};
            const auto it = dedup.find(k);
            if (it != dedup.end()) {
                return it->second;
            }
            MeshVertex mv{};
            mv.position[0] = positions[static_cast<std::size_t>(p)].x;
            mv.position[1] = positions[static_cast<std::size_t>(p)].y;
            mv.position[2] = positions[static_cast<std::size_t>(p)].z;
            if (n >= 0 && n < nN) {
                mv.normal[0] = normals[static_cast<std::size_t>(n)].x;
                mv.normal[1] = normals[static_cast<std::size_t>(n)].y;
                mv.normal[2] = normals[static_cast<std::size_t>(n)].z;
            }
            if (t >= 0 && t < nT) {
                mv.uv[0] = uvs[static_cast<std::size_t>(t)].x;
                mv.uv[1] = uvs[static_cast<std::size_t>(t)].y;
            }
            const auto newIdx = static_cast<std::uint32_t>(out.vertices.size());
            out.vertices.push_back(mv);
            dedup.emplace(k, newIdx);
            return newIdx;
        };

        const std::uint32_t i0 = emitVertex(verts[0]);
        for (std::size_t i = 2; i < verts.size(); ++i) {
            const std::uint32_t i1 = emitVertex(verts[i - 1]);
            const std::uint32_t i2 = emitVertex(verts[i]);
            out.indices.push_back(i0);
            out.indices.push_back(i1);
            out.indices.push_back(i2);
            out.submeshes.back().indexCount += 3;
        }
    };

    auto openSubmesh = [&](std::string materialName) {
        if (!out.submeshes.empty() && out.submeshes.back().indexCount == 0) {
            // Replace the empty trailing submesh in place.
            out.submeshes.back().materialName = std::move(materialName);
            return;
        }
        MeshSubmesh sm{};
        sm.firstIndex = static_cast<std::uint32_t>(out.indices.size());
        sm.materialName = std::move(materialName);
        out.submeshes.push_back(sm);
    };

    std::size_t pos = 0;
    while (pos < src.size()) {
        // Find end of line.
        std::size_t eol = pos;
        while (eol < src.size() && !isLineBreak(src[eol])) {
            ++eol;
        }
        std::string_view line = src.substr(pos, eol - pos);
        // Advance past the line break (LF or CRLF).
        pos = eol < src.size() ? eol + 1 : src.size();

        line = trimLeft(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        // Strip trailing CR if present.
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        const auto tag = nextToken(line);
        if (tag == "v") {
            const auto tx = nextToken(line);
            const auto ty = nextToken(line);
            const auto tz = nextToken(line);
            positions.push_back(Vec3{parseFloat(tx), parseFloat(ty), parseFloat(tz)});
        } else if (tag == "vn") {
            const auto tx = nextToken(line);
            const auto ty = nextToken(line);
            const auto tz = nextToken(line);
            Vec3 n{parseFloat(tx), parseFloat(ty), parseFloat(tz)};
            normalize(n);
            normals.push_back(n);
        } else if (tag == "vt") {
            const auto tu = nextToken(line);
            const auto tv = nextToken(line);
            uvs.push_back(Vec2{parseFloat(tu), parseFloat(tv)});
        } else if (tag == "f") {
            face.clear();
            for (;;) {
                const auto tok = nextToken(line);
                if (tok.empty()) break;
                face.push_back(parseFaceVertex(tok));
            }
            emitFace(face);
        } else if (tag == "usemtl") {
            const auto matTok = nextToken(line);
            openSubmesh(std::string(matTok));
        }
        // Silently skip other tags: o / g / mtllib / s / etc.
    }

    // Drop trailing empty submesh.
    if (!out.submeshes.empty() && out.submeshes.back().indexCount == 0) {
        out.submeshes.pop_back();
    }

    if (out.vertices.empty() || out.indices.empty()) {
        return AssetResult<MeshData>::failure(
            ErrorCode::ParseError, "OBJ contains no faces");
    }

    // Smoothed normals fallback when the source had no `vn` lines.
    if (normals.empty()) {
        std::vector<Vec3> accum(out.vertices.size(), Vec3{0, 0, 0});
        for (std::size_t i = 0; i + 2 < out.indices.size(); i += 3) {
            const auto i0 = out.indices[i];
            const auto i1 = out.indices[i + 1];
            const auto i2 = out.indices[i + 2];
            const Vec3 a{out.vertices[i0].position[0],
                         out.vertices[i0].position[1],
                         out.vertices[i0].position[2]};
            const Vec3 b{out.vertices[i1].position[0],
                         out.vertices[i1].position[1],
                         out.vertices[i1].position[2]};
            const Vec3 c{out.vertices[i2].position[0],
                         out.vertices[i2].position[1],
                         out.vertices[i2].position[2]};
            const Vec3 fn = faceNormal(a, b, c);
            accum[i0].x += fn.x; accum[i0].y += fn.y; accum[i0].z += fn.z;
            accum[i1].x += fn.x; accum[i1].y += fn.y; accum[i1].z += fn.z;
            accum[i2].x += fn.x; accum[i2].y += fn.y; accum[i2].z += fn.z;
        }
        for (std::size_t i = 0; i < out.vertices.size(); ++i) {
            normalize(accum[i]);
            out.vertices[i].normal[0] = accum[i].x;
            out.vertices[i].normal[1] = accum[i].y;
            out.vertices[i].normal[2] = accum[i].z;
        }
    }

    // AABB.
    constexpr float fmax = std::numeric_limits<float>::infinity();
    Aabb aabb{};
    aabb.min[0] = aabb.min[1] = aabb.min[2] =  fmax;
    aabb.max[0] = aabb.max[1] = aabb.max[2] = -fmax;
    for (const auto& v : out.vertices) {
        for (int k = 0; k < 3; ++k) {
            if (v.position[k] < aabb.min[k]) aabb.min[k] = v.position[k];
            if (v.position[k] > aabb.max[k]) aabb.max[k] = v.position[k];
        }
    }
    out.aabb = aabb;

    return AssetResult<MeshData>::success(std::move(out));
}

} // namespace

AssetResult<MeshData> parseObj(std::span<const std::byte> bytes,
                               std::string_view sourcePath) {
    return parseObjBytes(bytes, sourcePath);
}

AssetResult<MeshData> loadObj(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<MeshData>::failure(bytes.code,
                                              std::move(bytes.message));
    }
    return parseObjBytes(bytes.value, path);
}

} // namespace threadmaxx::assets
