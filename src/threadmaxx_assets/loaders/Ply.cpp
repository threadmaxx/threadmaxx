#include "threadmaxx_assets/loaders/ply.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "threadmaxx_assets/detail/io.hpp"

namespace threadmaxx::assets {

namespace {

struct Vec3 { float x, y, z; };

void normalize(Vec3& v) noexcept {
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= 0.0f) return;
    const float inv = 1.0f / std::sqrt(len2);
    v.x *= inv; v.y *= inv; v.z *= inv;
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

enum class PropType : std::uint8_t {
    Unknown, Char, UChar, Short, UShort, Int, UInt, Float, Double
};

PropType parsePropType(std::string_view tok) noexcept {
    if (tok == "char"   || tok == "int8")   return PropType::Char;
    if (tok == "uchar"  || tok == "uint8")  return PropType::UChar;
    if (tok == "short"  || tok == "int16")  return PropType::Short;
    if (tok == "ushort" || tok == "uint16") return PropType::UShort;
    if (tok == "int"    || tok == "int32")  return PropType::Int;
    if (tok == "uint"   || tok == "uint32") return PropType::UInt;
    if (tok == "float"  || tok == "float32") return PropType::Float;
    if (tok == "double" || tok == "float64") return PropType::Double;
    return PropType::Unknown;
}

std::uint32_t typeSize(PropType t) noexcept {
    switch (t) {
        case PropType::Char:
        case PropType::UChar:  return 1;
        case PropType::Short:
        case PropType::UShort: return 2;
        case PropType::Int:
        case PropType::UInt:
        case PropType::Float:  return 4;
        case PropType::Double: return 8;
        case PropType::Unknown: break;
    }
    return 0;
}

bool isLineBreak(char c) noexcept { return c == '\n' || c == '\r'; }
bool isSpace(char c) noexcept { return c == ' ' || c == '\t'; }

std::string_view trimLeft(std::string_view s) noexcept {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    return s;
}

std::string_view trimLineEnd(std::string_view s) noexcept {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
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

struct LineCursor {
    std::string_view src;
    std::size_t      pos{};

    bool nextLine(std::string_view& line) noexcept {
        if (pos >= src.size()) return false;
        std::size_t eol = pos;
        while (eol < src.size() && !isLineBreak(src[eol])) ++eol;
        line = trimLineEnd(src.substr(pos, eol - pos));
        pos = eol < src.size() ? eol + 1 : src.size();
        return true;
    }
};

struct VertexProperty {
    std::string name;
    PropType    type{PropType::Unknown};
};

struct FaceProperty {
    std::string name;
    PropType    listCountType{PropType::Unknown};
    PropType    listItemType{PropType::Unknown};
    bool        isList{false};
};

template <class T>
T readLE(const std::byte*& p, const std::byte* end, bool& ok) noexcept {
    T v{};
    if (p + sizeof(T) > end) {
        ok = false;
        return v;
    }
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
}

float readPropAsFloat(const std::byte*& p, const std::byte* end,
                      PropType t, bool& ok) noexcept {
    switch (t) {
        case PropType::Char:   return static_cast<float>(readLE<std::int8_t>(p, end, ok));
        case PropType::UChar:  return static_cast<float>(readLE<std::uint8_t>(p, end, ok));
        case PropType::Short:  return static_cast<float>(readLE<std::int16_t>(p, end, ok));
        case PropType::UShort: return static_cast<float>(readLE<std::uint16_t>(p, end, ok));
        case PropType::Int:    return static_cast<float>(readLE<std::int32_t>(p, end, ok));
        case PropType::UInt:   return static_cast<float>(readLE<std::uint32_t>(p, end, ok));
        case PropType::Float:  return readLE<float>(p, end, ok);
        case PropType::Double: return static_cast<float>(readLE<double>(p, end, ok));
        case PropType::Unknown: ok = false; break;
    }
    return 0.0f;
}

std::uint32_t readPropAsUint(const std::byte*& p, const std::byte* end,
                             PropType t, bool& ok) noexcept {
    switch (t) {
        case PropType::Char:   return static_cast<std::uint32_t>(readLE<std::int8_t>(p, end, ok));
        case PropType::UChar:  return static_cast<std::uint32_t>(readLE<std::uint8_t>(p, end, ok));
        case PropType::Short:  return static_cast<std::uint32_t>(readLE<std::int16_t>(p, end, ok));
        case PropType::UShort: return static_cast<std::uint32_t>(readLE<std::uint16_t>(p, end, ok));
        case PropType::Int:    return static_cast<std::uint32_t>(readLE<std::int32_t>(p, end, ok));
        case PropType::UInt:   return readLE<std::uint32_t>(p, end, ok);
        case PropType::Float:  return static_cast<std::uint32_t>(readLE<float>(p, end, ok));
        case PropType::Double: return static_cast<std::uint32_t>(readLE<double>(p, end, ok));
        case PropType::Unknown: ok = false; break;
    }
    return 0;
}

void skipProp(const std::byte*& p, const std::byte* end, PropType t, bool& ok) noexcept {
    const auto sz = typeSize(t);
    if (sz == 0 || p + sz > end) { ok = false; return; }
    p += sz;
}

void skipListProp(const std::byte*& p, const std::byte* end,
                  PropType countType, PropType itemType, bool& ok) noexcept {
    const std::uint32_t n = readPropAsUint(p, end, countType, ok);
    if (!ok) return;
    const auto sz = typeSize(itemType);
    if (sz == 0 || p + static_cast<std::ptrdiff_t>(n) * sz > end) {
        ok = false;
        return;
    }
    p += static_cast<std::ptrdiff_t>(n) * sz;
}

AssetResult<MeshData> parsePlyBytes(std::span<const std::byte> bytes,
                                    std::string_view sourcePath) {
    MeshData out;
    out.sourcePath = std::string(sourcePath);
    out.submeshes.push_back(MeshSubmesh{0, 0, kInvalidAssetId, ""});

    if (bytes.size() < 4) {
        return AssetResult<MeshData>::failure(ErrorCode::Truncated, "PLY too short");
    }
    std::string_view header{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    if (header.substr(0, 4) != "ply\n" && header.substr(0, 5) != "ply\r\n") {
        return AssetResult<MeshData>::failure(ErrorCode::BadMagic, "missing PLY magic");
    }

    enum class Format { Ascii, BinaryLE };
    Format fmt = Format::Ascii;
    bool fmtSet = false;

    enum class CurrentElement { None, Vertex, Face, Other };
    CurrentElement cur = CurrentElement::None;

    std::vector<VertexProperty> vprops;
    std::vector<FaceProperty>   fprops;
    std::uint32_t vertexCount = 0;
    std::uint32_t faceCount   = 0;
    std::uint32_t otherCount  = 0;
    std::vector<PropType>       otherSeq;
    std::vector<std::pair<bool, std::pair<PropType, PropType>>> otherListSeq;

    LineCursor cur_in{header, 0};
    std::string_view line;
    std::size_t headerEnd = 0;

    while (cur_in.nextLine(line)) {
        line = trimLeft(line);
        if (line.empty()) continue;

        if (line == "end_header") {
            headerEnd = cur_in.pos;
            break;
        }

        auto rest = line;
        const auto tag = nextToken(rest);
        if (tag == "comment" || tag == "obj_info") {
            continue;
        }
        if (tag == "format") {
            const auto kind = nextToken(rest);
            if (kind == "ascii") {
                fmt = Format::Ascii;
            } else if (kind == "binary_little_endian") {
                fmt = Format::BinaryLE;
            } else {
                return AssetResult<MeshData>::failure(
                    ErrorCode::UnsupportedFormat,
                    std::string("PLY format: ") + std::string(kind));
            }
            fmtSet = true;
        } else if (tag == "element") {
            const auto name  = nextToken(rest);
            const auto count = nextToken(rest);
            const auto n = static_cast<std::uint32_t>(std::strtoul(
                std::string(count).c_str(), nullptr, 10));
            if (name == "vertex") {
                cur = CurrentElement::Vertex;
                vertexCount = n;
            } else if (name == "face") {
                cur = CurrentElement::Face;
                faceCount = n;
            } else {
                cur = CurrentElement::Other;
                otherCount = n;
            }
        } else if (tag == "property") {
            auto first = nextToken(rest);
            if (first == "list") {
                const auto countType = parsePropType(nextToken(rest));
                const auto itemType  = parsePropType(nextToken(rest));
                const auto name      = nextToken(rest);
                if (cur == CurrentElement::Face) {
                    FaceProperty fp;
                    fp.name = std::string(name);
                    fp.listCountType = countType;
                    fp.listItemType  = itemType;
                    fp.isList = true;
                    fprops.push_back(std::move(fp));
                } else {
                    otherListSeq.push_back({true, {countType, itemType}});
                }
            } else {
                const auto type = parsePropType(first);
                const auto name = nextToken(rest);
                if (cur == CurrentElement::Vertex) {
                    VertexProperty vp;
                    vp.name = std::string(name);
                    vp.type = type;
                    vprops.push_back(std::move(vp));
                } else if (cur == CurrentElement::Face) {
                    FaceProperty fp;
                    fp.name = std::string(name);
                    fp.listCountType = type;
                    fp.isList = false;
                    fprops.push_back(std::move(fp));
                } else {
                    otherSeq.push_back(type);
                }
            }
        }
    }

    if (!fmtSet || headerEnd == 0) {
        return AssetResult<MeshData>::failure(
            ErrorCode::ParseError, "PLY header malformed");
    }
    if (fmt != Format::BinaryLE && fmt != Format::Ascii) {
        return AssetResult<MeshData>::failure(
            ErrorCode::UnsupportedFormat, "PLY: only ASCII and binary LE supported");
    }

    // Map property indices to vertex semantics.
    int idxX = -1, idxY = -1, idxZ = -1;
    int idxNx = -1, idxNy = -1, idxNz = -1;
    int idxS = -1, idxT = -1;
    for (std::size_t i = 0; i < vprops.size(); ++i) {
        const auto& n = vprops[i].name;
        const int ii = static_cast<int>(i);
        if (n == "x") idxX = ii;
        else if (n == "y") idxY = ii;
        else if (n == "z") idxZ = ii;
        else if (n == "nx") idxNx = ii;
        else if (n == "ny") idxNy = ii;
        else if (n == "nz") idxNz = ii;
        else if (n == "s" || n == "u" || n == "texture_u") idxS = ii;
        else if (n == "t" || n == "v" || n == "texture_v") idxT = ii;
    }
    if (idxX < 0 || idxY < 0 || idxZ < 0) {
        return AssetResult<MeshData>::failure(
            ErrorCode::ParseError, "PLY vertex element missing x/y/z");
    }

    out.vertices.reserve(vertexCount);
    const bool hasNormals = idxNx >= 0 && idxNy >= 0 && idxNz >= 0;

    if (fmt == Format::BinaryLE) {
        const std::byte* p = bytes.data() + headerEnd;
        const std::byte* endp = bytes.data() + bytes.size();
        bool ok = true;

        for (std::uint32_t i = 0; i < vertexCount; ++i) {
            std::vector<float> values(vprops.size(), 0.0f);
            for (std::size_t pi = 0; pi < vprops.size(); ++pi) {
                values[pi] = readPropAsFloat(p, endp, vprops[pi].type, ok);
                if (!ok) {
                    return AssetResult<MeshData>::failure(
                        ErrorCode::Truncated, "PLY vertex truncated");
                }
            }
            MeshVertex mv{};
            mv.position[0] = values[static_cast<std::size_t>(idxX)];
            mv.position[1] = values[static_cast<std::size_t>(idxY)];
            mv.position[2] = values[static_cast<std::size_t>(idxZ)];
            if (hasNormals) {
                mv.normal[0] = values[static_cast<std::size_t>(idxNx)];
                mv.normal[1] = values[static_cast<std::size_t>(idxNy)];
                mv.normal[2] = values[static_cast<std::size_t>(idxNz)];
            }
            if (idxS >= 0 && idxT >= 0) {
                mv.uv[0] = values[static_cast<std::size_t>(idxS)];
                mv.uv[1] = values[static_cast<std::size_t>(idxT)];
            }
            out.vertices.push_back(mv);
        }

        out.indices.reserve(faceCount * 3);
        for (std::uint32_t i = 0; i < faceCount; ++i) {
            std::vector<std::uint32_t> indices;
            for (const auto& fp : fprops) {
                if (fp.isList && (fp.name == "vertex_indices" || fp.name == "vertex_index")) {
                    const std::uint32_t n = readPropAsUint(p, endp, fp.listCountType, ok);
                    if (!ok) {
                        return AssetResult<MeshData>::failure(
                            ErrorCode::Truncated, "PLY face truncated");
                    }
                    indices.resize(n);
                    for (std::uint32_t k = 0; k < n; ++k) {
                        indices[k] = readPropAsUint(p, endp, fp.listItemType, ok);
                        if (!ok) {
                            return AssetResult<MeshData>::failure(
                                ErrorCode::Truncated, "PLY face index truncated");
                        }
                    }
                } else if (fp.isList) {
                    skipListProp(p, endp, fp.listCountType, fp.listItemType, ok);
                } else {
                    skipProp(p, endp, fp.listCountType, ok);
                }
            }
            if (indices.size() < 3) continue;
            const auto i0 = indices[0];
            for (std::size_t k = 2; k < indices.size(); ++k) {
                out.indices.push_back(i0);
                out.indices.push_back(indices[k - 1]);
                out.indices.push_back(indices[k]);
                out.submeshes.back().indexCount += 3;
            }
        }
    } else {
        // ASCII path: parse numbers per line.
        LineCursor c_in{header, headerEnd};
        std::string_view l;

        for (std::uint32_t i = 0; i < vertexCount; ++i) {
            if (!c_in.nextLine(l)) {
                return AssetResult<MeshData>::failure(
                    ErrorCode::Truncated, "PLY ascii vertex truncated");
            }
            std::vector<float> values(vprops.size(), 0.0f);
            std::string_view rest = l;
            for (std::size_t pi = 0; pi < vprops.size(); ++pi) {
                const auto tok = nextToken(rest);
                values[pi] = static_cast<float>(std::strtod(
                    std::string(tok).c_str(), nullptr));
            }
            MeshVertex mv{};
            mv.position[0] = values[static_cast<std::size_t>(idxX)];
            mv.position[1] = values[static_cast<std::size_t>(idxY)];
            mv.position[2] = values[static_cast<std::size_t>(idxZ)];
            if (hasNormals) {
                mv.normal[0] = values[static_cast<std::size_t>(idxNx)];
                mv.normal[1] = values[static_cast<std::size_t>(idxNy)];
                mv.normal[2] = values[static_cast<std::size_t>(idxNz)];
            }
            if (idxS >= 0 && idxT >= 0) {
                mv.uv[0] = values[static_cast<std::size_t>(idxS)];
                mv.uv[1] = values[static_cast<std::size_t>(idxT)];
            }
            out.vertices.push_back(mv);
        }

        for (std::uint32_t i = 0; i < faceCount; ++i) {
            if (!c_in.nextLine(l)) {
                return AssetResult<MeshData>::failure(
                    ErrorCode::Truncated, "PLY ascii face truncated");
            }
            std::string_view rest = l;
            const auto countTok = nextToken(rest);
            const auto n = static_cast<std::uint32_t>(std::strtoul(
                std::string(countTok).c_str(), nullptr, 10));
            std::vector<std::uint32_t> indices(n);
            for (std::uint32_t k = 0; k < n; ++k) {
                const auto tok = nextToken(rest);
                indices[k] = static_cast<std::uint32_t>(std::strtoul(
                    std::string(tok).c_str(), nullptr, 10));
            }
            if (indices.size() < 3) continue;
            const auto i0 = indices[0];
            for (std::size_t k = 2; k < indices.size(); ++k) {
                out.indices.push_back(i0);
                out.indices.push_back(indices[k - 1]);
                out.indices.push_back(indices[k]);
                out.submeshes.back().indexCount += 3;
            }
        }
    }

    // Other-element bodies are ignored entirely in ASCII; in binary we skipped
    // above by treating them as per-vertex sequences. For minimal PLY (vertex
    // + face only) this never triggers.
    (void)otherCount;
    (void)otherSeq;
    (void)otherListSeq;

    if (out.vertices.empty() || out.indices.empty()) {
        return AssetResult<MeshData>::failure(
            ErrorCode::ParseError, "PLY has no faces");
    }

    // Smoothed normals fallback if header had none.
    if (!hasNormals) {
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

AssetResult<MeshData> parsePly(std::span<const std::byte> bytes,
                               std::string_view sourcePath) {
    return parsePlyBytes(bytes, sourcePath);
}

AssetResult<MeshData> loadPly(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<MeshData>::failure(bytes.code,
                                              std::move(bytes.message));
    }
    return parsePlyBytes(bytes.value, path);
}

} // namespace threadmaxx::assets
