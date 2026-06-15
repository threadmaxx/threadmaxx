#include "threadmaxx_assets/loaders/gltf.hpp"

#include "threadmaxx_assets/detail/io.hpp"
#include "threadmaxx_assets/detail/minimal_json.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace threadmaxx::assets {

namespace {

using detail::JsonValue;

// -- .glb container ------------------------------------------------------

struct GlbChunks {
    std::string_view json;
    std::span<const std::byte> bin;
};

constexpr std::uint32_t kGlbMagic     = 0x46546C67u;     // 'glTF'
constexpr std::uint32_t kGlbVersion   = 2u;
constexpr std::uint32_t kChunkJson    = 0x4E4F534Au;     // 'JSON'
constexpr std::uint32_t kChunkBin     = 0x004E4942u;     // 'BIN\0'

std::uint32_t readU32LE(const std::byte* p) noexcept {
    std::uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

bool readGlbContainer(std::span<const std::byte> bytes,
                      GlbChunks& out,
                      std::string& err) noexcept {
    if (bytes.size() < 12) { err = "glb header too short"; return false; }
    const std::uint32_t magic = readU32LE(bytes.data());
    const std::uint32_t ver   = readU32LE(bytes.data() + 4);
    const std::uint32_t len   = readU32LE(bytes.data() + 8);
    if (magic != kGlbMagic) { err = "bad glb magic"; return false; }
    if (ver != kGlbVersion) { err = "unsupported glb version"; return false; }
    if (len > bytes.size()) { err = "glb declared length exceeds buffer"; return false; }

    std::size_t cur = 12;
    bool sawJson = false;
    while (cur + 8 <= len) {
        const std::uint32_t chunkLen  = readU32LE(bytes.data() + cur);
        const std::uint32_t chunkType = readU32LE(bytes.data() + cur + 4);
        cur += 8;
        if (cur + chunkLen > len) { err = "glb chunk overruns container"; return false; }
        const std::byte* payload = bytes.data() + cur;
        if (chunkType == kChunkJson) {
            out.json = std::string_view(reinterpret_cast<const char*>(payload), chunkLen);
            sawJson = true;
        } else if (chunkType == kChunkBin) {
            out.bin = std::span<const std::byte>(payload, chunkLen);
        }
        // unknown chunks: silently skip (spec-permitted)
        cur += chunkLen;
    }
    if (!sawJson) { err = "glb missing JSON chunk"; return false; }
    return true;
}

// -- accessor decode -----------------------------------------------------

constexpr int kCompTypeByte       = 5120;
constexpr int kCompTypeUByte      = 5121;
constexpr int kCompTypeShort      = 5122;
constexpr int kCompTypeUShort     = 5123;
constexpr int kCompTypeUInt       = 5125;
constexpr int kCompTypeFloat      = 5126;

int componentSize(int t) noexcept {
    switch (t) {
        case kCompTypeByte:   case kCompTypeUByte:  return 1;
        case kCompTypeShort:  case kCompTypeUShort: return 2;
        case kCompTypeUInt:   case kCompTypeFloat:  return 4;
        default: return 0;
    }
}

int typeComponentCount(std::string_view type) noexcept {
    if (type == "SCALAR") return 1;
    if (type == "VEC2")   return 2;
    if (type == "VEC3")   return 3;
    if (type == "VEC4")   return 4;
    if (type == "MAT2")   return 4;
    if (type == "MAT3")   return 9;
    if (type == "MAT4")   return 16;
    return 0;
}

// Parsed accessor metadata. Stride==0 means tightly packed (we compute
// element size from componentType * count).
struct Accessor {
    int            bufferViewIndex{-1};
    int            componentType{};
    int            count{};
    std::string    type;
    int            byteOffset{};
    bool           normalized{};
};

struct BufferView {
    int             buffer{-1};
    std::size_t     byteOffset{};
    std::size_t     byteLength{};
    std::size_t     byteStride{};
};

// Pull one component value as float. `comp` is the component index
// inside an element (e.g. 0..2 for VEC3). `componentType` selects the
// raw width; `normalized` rescales integer attrs to [0,1] or [-1,1]
// per glTF spec.
float readFloatComponent(const std::byte* base,
                         int componentType,
                         bool normalized,
                         int comp) noexcept {
    const std::byte* p = base + comp * componentSize(componentType);
    switch (componentType) {
        case kCompTypeFloat: {
            float v = 0; std::memcpy(&v, p, 4); return v;
        }
        case kCompTypeUByte: {
            std::uint8_t v = 0; std::memcpy(&v, p, 1);
            return normalized ? static_cast<float>(v) / 255.0f
                              : static_cast<float>(v);
        }
        case kCompTypeByte: {
            std::int8_t v = 0; std::memcpy(&v, p, 1);
            return normalized ? std::fmaxf(static_cast<float>(v) / 127.0f, -1.0f)
                              : static_cast<float>(v);
        }
        case kCompTypeUShort: {
            std::uint16_t v = 0; std::memcpy(&v, p, 2);
            return normalized ? static_cast<float>(v) / 65535.0f
                              : static_cast<float>(v);
        }
        case kCompTypeShort: {
            std::int16_t v = 0; std::memcpy(&v, p, 2);
            return normalized ? std::fmaxf(static_cast<float>(v) / 32767.0f, -1.0f)
                              : static_cast<float>(v);
        }
        case kCompTypeUInt: {
            std::uint32_t v = 0; std::memcpy(&v, p, 4);
            return static_cast<float>(v);
        }
    }
    return 0.0f;
}

std::uint32_t readUintComponent(const std::byte* base,
                                int componentType,
                                int comp) noexcept {
    const std::byte* p = base + comp * componentSize(componentType);
    switch (componentType) {
        case kCompTypeUByte: {
            std::uint8_t v = 0; std::memcpy(&v, p, 1); return v;
        }
        case kCompTypeUShort: {
            std::uint16_t v = 0; std::memcpy(&v, p, 2); return v;
        }
        case kCompTypeUInt: {
            std::uint32_t v = 0; std::memcpy(&v, p, 4); return v;
        }
    }
    return 0;
}

// -- json helpers --------------------------------------------------------

const JsonValue* findInObject(const JsonValue& obj, std::string_view key) noexcept {
    return obj.isObject() ? obj.find(key) : nullptr;
}

int asInt(const JsonValue* v, int fallback = 0) noexcept {
    if (v && v->isNumber()) return static_cast<int>(v->asNumber());
    return fallback;
}

std::string asString(const JsonValue* v) {
    if (v && v->isString()) return v->asString();
    return {};
}

bool asBool(const JsonValue* v, bool fallback = false) noexcept {
    if (v && v->isBool()) return v->asBool();
    return fallback;
}

const std::vector<JsonValue>& asArrayOrEmpty(const JsonValue* v) noexcept {
    static const std::vector<JsonValue> kEmpty;
    if (v && v->isArray()) return v->asArray();
    return kEmpty;
}

void readVec3(const JsonValue* v, float out[3]) noexcept {
    if (!v || !v->isArray()) return;
    const auto& arr = v->asArray();
    for (std::size_t i = 0; i < arr.size() && i < 3; ++i) {
        if (arr[i].isNumber()) out[i] = static_cast<float>(arr[i].asNumber());
    }
}

void readVec4(const JsonValue* v, float out[4]) noexcept {
    if (!v || !v->isArray()) return;
    const auto& arr = v->asArray();
    for (std::size_t i = 0; i < arr.size() && i < 4; ++i) {
        if (arr[i].isNumber()) out[i] = static_cast<float>(arr[i].asNumber());
    }
}

void readMat4(const JsonValue* v, float out[16]) noexcept {
    if (!v || !v->isArray()) return;
    const auto& arr = v->asArray();
    for (std::size_t i = 0; i < arr.size() && i < 16; ++i) {
        if (arr[i].isNumber()) out[i] = static_cast<float>(arr[i].asNumber());
    }
}

// -- math helpers (column-major 4x4) -------------------------------------

void identity4(float m[16]) noexcept {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mul4(const float a[16], const float b[16], float out[16]) noexcept {
    float tmp[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += a[k * 4 + r] * b[c * 4 + k];
            }
            tmp[c * 4 + r] = s;
        }
    }
    std::memcpy(out, tmp, sizeof(tmp));
}

void trsToMat4(const float t[3], const float r[4], const float s[3], float out[16]) noexcept {
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    // Column 0
    out[0]  = (1.0f - 2.0f * (yy + zz)) * s[0];
    out[1]  = (2.0f * (xy + wz)) * s[0];
    out[2]  = (2.0f * (xz - wy)) * s[0];
    out[3]  = 0.0f;
    // Column 1
    out[4]  = (2.0f * (xy - wz)) * s[1];
    out[5]  = (1.0f - 2.0f * (xx + zz)) * s[1];
    out[6]  = (2.0f * (yz + wx)) * s[1];
    out[7]  = 0.0f;
    // Column 2
    out[8]  = (2.0f * (xz + wy)) * s[2];
    out[9]  = (2.0f * (yz - wx)) * s[2];
    out[10] = (1.0f - 2.0f * (xx + yy)) * s[2];
    out[11] = 0.0f;
    // Column 3 (translation)
    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    out[15] = 1.0f;
}

void transformPoint(const float m[16], const float p[3], float out[3]) noexcept {
    const float x = p[0], y = p[1], z = p[2];
    out[0] = m[0] * x + m[4] * y + m[8]  * z + m[12];
    out[1] = m[1] * x + m[5] * y + m[9]  * z + m[13];
    out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}

void transformDirection(const float m[16], const float d[3], float out[3]) noexcept {
    const float x = d[0], y = d[1], z = d[2];
    out[0] = m[0] * x + m[4] * y + m[8]  * z;
    out[1] = m[1] * x + m[5] * y + m[9]  * z;
    out[2] = m[2] * x + m[6] * y + m[10] * z;
}

// -- main parse ----------------------------------------------------------

struct ParseCtx {
    const std::vector<Accessor>&         accessors;
    const std::vector<BufferView>&       bufferViews;
    std::span<const std::byte>           bin;
};

// Get a pointer to the start of element `elementIndex` inside an
// accessor. Returns nullptr if the accessor / bufferView / buffer are
// inconsistent.
const std::byte* accessorElement(const ParseCtx& ctx,
                                 int accessorIndex,
                                 int elementIndex,
                                 std::size_t& outStride) noexcept {
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(ctx.accessors.size()))
        return nullptr;
    const auto& a = ctx.accessors[static_cast<std::size_t>(accessorIndex)];
    if (a.bufferViewIndex < 0 ||
        a.bufferViewIndex >= static_cast<int>(ctx.bufferViews.size()))
        return nullptr;
    const auto& bv = ctx.bufferViews[static_cast<std::size_t>(a.bufferViewIndex)];
    const int elemBytes = typeComponentCount(a.type) * componentSize(a.componentType);
    if (elemBytes == 0) return nullptr;
    const std::size_t stride = bv.byteStride != 0
        ? bv.byteStride
        : static_cast<std::size_t>(elemBytes);
    outStride = stride;
    const std::size_t offset = bv.byteOffset +
        static_cast<std::size_t>(a.byteOffset) +
        static_cast<std::size_t>(elementIndex) * stride;
    if (offset + static_cast<std::size_t>(elemBytes) > ctx.bin.size())
        return nullptr;
    return ctx.bin.data() + offset;
}

bool decodeFloatAccessor(const ParseCtx& ctx, int accessorIndex,
                         int compCount, std::vector<float>& out) {
    if (accessorIndex < 0) return false;
    const auto& a = ctx.accessors[static_cast<std::size_t>(accessorIndex)];
    const std::size_t cc = static_cast<std::size_t>(compCount);
    out.resize(static_cast<std::size_t>(a.count) * cc);
    for (int i = 0; i < a.count; ++i) {
        std::size_t stride = 0;
        const std::byte* base = accessorElement(ctx, accessorIndex, i, stride);
        if (!base) return false;
        for (int c = 0; c < compCount; ++c) {
            out[static_cast<std::size_t>(i) * cc + static_cast<std::size_t>(c)] =
                readFloatComponent(base, a.componentType, a.normalized, c);
        }
    }
    return true;
}

bool decodeIndexAccessor(const ParseCtx& ctx, int accessorIndex,
                         std::uint32_t baseVertex, std::vector<std::uint32_t>& outAppend) {
    if (accessorIndex < 0) return false;
    const auto& a = ctx.accessors[static_cast<std::size_t>(accessorIndex)];
    outAppend.reserve(outAppend.size() + static_cast<std::size_t>(a.count));
    for (int i = 0; i < a.count; ++i) {
        std::size_t stride = 0;
        const std::byte* base = accessorElement(ctx, accessorIndex, i, stride);
        if (!base) return false;
        outAppend.push_back(baseVertex + readUintComponent(base, a.componentType, 0));
    }
    return true;
}

// Build a 4x4 world matrix for `nodeIndex` by walking up parent chain.
void computeWorldMatrix(int nodeIndex,
                        const std::vector<JsonValue>& nodes,
                        const std::vector<int>& parentOf,
                        float out[16]) noexcept {
    // Build local matrix from `matrix` if present; else from TRS.
    auto buildLocal = [&](int n, float m[16]) {
        identity4(m);
        if (n < 0 || n >= static_cast<int>(nodes.size())) return;
        const auto& node = nodes[static_cast<std::size_t>(n)];
        if (const auto* mat = findInObject(node, "matrix")) {
            readMat4(mat, m);
            return;
        }
        float t[3] = {0, 0, 0};
        float r[4] = {0, 0, 0, 1};
        float s[3] = {1, 1, 1};
        readVec3(findInObject(node, "translation"), t);
        readVec4(findInObject(node, "rotation"),    r);
        readVec3(findInObject(node, "scale"),       s);
        trsToMat4(t, r, s, m);
    };

    identity4(out);
    // Walk up; collect chain, then multiply root → leaf.
    int chain[64];
    int chainLen = 0;
    int cur = nodeIndex;
    while (cur >= 0 && chainLen < 64) {
        chain[chainLen++] = cur;
        cur = parentOf[static_cast<std::size_t>(cur)];
    }
    float acc[16];
    identity4(acc);
    for (int i = chainLen - 1; i >= 0; --i) {
        float local[16];
        buildLocal(chain[i], local);
        float next[16];
        mul4(acc, local, next);
        std::memcpy(acc, next, sizeof(acc));
    }
    std::memcpy(out, acc, sizeof(acc));
}

AssetResult<GltfSceneData> parseScene(std::span<const std::byte> bytes,
                                      std::string_view sourcePath) {
    GltfSceneData scene;
    scene.sourcePath = std::string(sourcePath);

    std::string err;
    GlbChunks chunks;
    if (!readGlbContainer(bytes, chunks, err)) {
        return AssetResult<GltfSceneData>::failure(ErrorCode::BadMagic, std::move(err));
    }

    JsonValue root;
    if (!detail::parseJson(chunks.json, root) || !root.isObject()) {
        return AssetResult<GltfSceneData>::failure(ErrorCode::ParseError,
                                                   "glTF JSON parse failed");
    }

    // accessors[], bufferViews[]
    std::vector<Accessor> accessors;
    for (const auto& a : asArrayOrEmpty(root.find("accessors"))) {
        Accessor acc;
        acc.bufferViewIndex = asInt(findInObject(a, "bufferView"), -1);
        acc.componentType   = asInt(findInObject(a, "componentType"));
        acc.count           = asInt(findInObject(a, "count"));
        acc.type            = asString(findInObject(a, "type"));
        acc.byteOffset      = asInt(findInObject(a, "byteOffset"));
        acc.normalized      = asBool(findInObject(a, "normalized"));
        accessors.push_back(std::move(acc));
    }
    std::vector<BufferView> bufferViews;
    for (const auto& bv : asArrayOrEmpty(root.find("bufferViews"))) {
        BufferView v;
        v.buffer     = asInt(findInObject(bv, "buffer"), -1);
        v.byteOffset = static_cast<std::size_t>(asInt(findInObject(bv, "byteOffset")));
        v.byteLength = static_cast<std::size_t>(asInt(findInObject(bv, "byteLength")));
        v.byteStride = static_cast<std::size_t>(asInt(findInObject(bv, "byteStride")));
        bufferViews.push_back(v);
    }

    ParseCtx ctx{accessors, bufferViews, chunks.bin};

    // -- skins -----------------------------------------------------------
    const auto& nodes = asArrayOrEmpty(root.find("nodes"));
    // Parent table: -1 == no parent.
    std::vector<int> parentOf(nodes.size(), -1);
    for (int n = 0; n < static_cast<int>(nodes.size()); ++n) {
        for (const auto& child :
             asArrayOrEmpty(findInObject(nodes[static_cast<std::size_t>(n)], "children"))) {
            if (child.isNumber()) {
                const int c = static_cast<int>(child.asNumber());
                if (c >= 0 && c < static_cast<int>(parentOf.size())) {
                    parentOf[static_cast<std::size_t>(c)] = n;
                }
            }
        }
    }

    const auto& skinArr = asArrayOrEmpty(root.find("skins"));
    for (const auto& sk : skinArr) {
        SkinData skin;
        skin.name = asString(findInObject(sk, "name"));
        const auto& jointArr = asArrayOrEmpty(findInObject(sk, "joints"));
        skin.joints.reserve(jointArr.size());
        // Build remap: scene-node-id → joint-list index.
        std::vector<int> nodeToJoint(nodes.size(), -1);
        for (std::size_t i = 0; i < jointArr.size(); ++i) {
            if (!jointArr[i].isNumber()) continue;
            const int n = static_cast<int>(jointArr[i].asNumber());
            if (n < 0 || n >= static_cast<int>(nodes.size())) continue;
            nodeToJoint[static_cast<std::size_t>(n)] = static_cast<int>(i);
        }
        for (std::size_t i = 0; i < jointArr.size(); ++i) {
            if (!jointArr[i].isNumber()) continue;
            const int n = static_cast<int>(jointArr[i].asNumber());
            GltfJoint joint;
            if (n >= 0 && n < static_cast<int>(nodes.size())) {
                const auto& node = nodes[static_cast<std::size_t>(n)];
                joint.name = asString(findInObject(node, "name"));
                // Parent within joint list. Walk up until parent node
                // is in the joint set.
                int p = parentOf[static_cast<std::size_t>(n)];
                while (p >= 0 && nodeToJoint[static_cast<std::size_t>(p)] < 0) {
                    p = parentOf[static_cast<std::size_t>(p)];
                }
                joint.parent = p < 0 ? -1 : nodeToJoint[static_cast<std::size_t>(p)];
                readVec3(findInObject(node, "translation"), joint.translation);
                readVec4(findInObject(node, "rotation"),    joint.rotation);
                readVec3(findInObject(node, "scale"),       joint.scale);
            }
            skin.joints.push_back(std::move(joint));
        }
        // inverseBindMatrices
        const int ibm = asInt(findInObject(sk, "inverseBindMatrices"), -1);
        if (ibm >= 0) {
            std::vector<float> raw;
            if (decodeFloatAccessor(ctx, ibm, 16, raw)) {
                const std::size_t n =
                    std::min(skin.joints.size(), raw.size() / 16);
                for (std::size_t i = 0; i < n; ++i) {
                    std::memcpy(skin.joints[i].inverseBind, raw.data() + i * 16,
                                sizeof(float) * 16);
                }
            }
        }
        scene.skins.push_back(std::move(skin));
    }

    // -- meshes ---------------------------------------------------------
    // First, build mesh-index → owning node (and that node's world
    // matrix) so we can bake transforms into vertex positions.
    std::vector<int> meshOwnerNode(asArrayOrEmpty(root.find("meshes")).size(), -1);
    for (int n = 0; n < static_cast<int>(nodes.size()); ++n) {
        const int meshIdx = asInt(
            findInObject(nodes[static_cast<std::size_t>(n)], "mesh"), -1);
        if (meshIdx >= 0 && meshIdx < static_cast<int>(meshOwnerNode.size())) {
            meshOwnerNode[static_cast<std::size_t>(meshIdx)] = n;
        }
    }
    // Also remember skin per node so meshes inherit the skin index.
    auto nodeSkin = [&](int n) -> int {
        if (n < 0 || n >= static_cast<int>(nodes.size())) return -1;
        return asInt(findInObject(nodes[static_cast<std::size_t>(n)], "skin"), -1);
    };

    const auto& meshArr = asArrayOrEmpty(root.find("meshes"));
    for (int mi = 0; mi < static_cast<int>(meshArr.size()); ++mi) {
        const auto& m = meshArr[static_cast<std::size_t>(mi)];
        GltfMesh out;
        out.mesh.sourcePath = std::string(sourcePath);

        const int ownerNode = meshOwnerNode[static_cast<std::size_t>(mi)];
        float world[16]; identity4(world);
        if (ownerNode >= 0) {
            computeWorldMatrix(ownerNode, nodes, parentOf, world);
        }
        const int skin = nodeSkin(ownerNode);
        out.skinIndex = skin >= 0 ? static_cast<AssetId>(skin) : kInvalidAssetId;
        const bool isSkinned = skin >= 0;

        for (const auto& prim : asArrayOrEmpty(findInObject(m, "primitives"))) {
            const std::uint32_t baseVertex =
                static_cast<std::uint32_t>(out.mesh.vertices.size());
            const std::size_t baseIndex = out.mesh.indices.size();

            const JsonValue* attrs = findInObject(prim, "attributes");
            const int aPos = attrs ? asInt(attrs->find("POSITION"), -1)   : -1;
            const int aNrm = attrs ? asInt(attrs->find("NORMAL"), -1)     : -1;
            const int aUv  = attrs ? asInt(attrs->find("TEXCOORD_0"), -1) : -1;
            const int aJ0  = attrs ? asInt(attrs->find("JOINTS_0"), -1)   : -1;
            const int aW0  = attrs ? asInt(attrs->find("WEIGHTS_0"), -1)  : -1;
            if (aPos < 0) continue;

            std::vector<float> positions, normals, uvs;
            decodeFloatAccessor(ctx, aPos, 3, positions);
            if (aNrm >= 0) decodeFloatAccessor(ctx, aNrm, 3, normals);
            if (aUv  >= 0) decodeFloatAccessor(ctx, aUv,  2, uvs);

            const std::size_t vertCount = positions.size() / 3;
            for (std::size_t v = 0; v < vertCount; ++v) {
                MeshVertex mv{};
                const float p[3] = {positions[v * 3 + 0],
                                    positions[v * 3 + 1],
                                    positions[v * 3 + 2]};
                if (!isSkinned) {
                    transformPoint(world, p, mv.position);
                } else {
                    mv.position[0] = p[0];
                    mv.position[1] = p[1];
                    mv.position[2] = p[2];
                }
                if (aNrm >= 0 && v * 3 + 2 < normals.size()) {
                    const float n[3] = {normals[v * 3 + 0],
                                        normals[v * 3 + 1],
                                        normals[v * 3 + 2]};
                    float wn[3]{};
                    if (!isSkinned) {
                        transformDirection(world, n, wn);
                    } else {
                        wn[0] = n[0]; wn[1] = n[1]; wn[2] = n[2];
                    }
                    const float len2 = wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2];
                    if (len2 > 0.0f) {
                        const float inv = 1.0f / std::sqrt(len2);
                        mv.normal[0] = wn[0] * inv;
                        mv.normal[1] = wn[1] * inv;
                        mv.normal[2] = wn[2] * inv;
                    }
                } else {
                    mv.normal[0] = 0.0f; mv.normal[1] = 1.0f; mv.normal[2] = 0.0f;
                }
                if (aUv >= 0 && v * 2 + 1 < uvs.size()) {
                    mv.uv[0] = uvs[v * 2 + 0];
                    mv.uv[1] = uvs[v * 2 + 1];
                }
                out.mesh.vertices.push_back(mv);
            }

            // Skinning attrs — parallel to vertices.
            if (aJ0 >= 0 && aW0 >= 0) {
                out.skinned.resize(out.mesh.vertices.size());
                const Accessor& jA = accessors[static_cast<std::size_t>(aJ0)];
                const Accessor& wA = accessors[static_cast<std::size_t>(aW0)];
                for (std::size_t v = 0; v < vertCount; ++v) {
                    std::size_t strideJ = 0, strideW = 0;
                    const std::byte* jb = accessorElement(ctx, aJ0,
                                                          static_cast<int>(v), strideJ);
                    const std::byte* wb = accessorElement(ctx, aW0,
                                                          static_cast<int>(v), strideW);
                    SkinnedVertex sv{};
                    if (jb) {
                        for (int c = 0; c < 4; ++c) {
                            sv.joints[c] = static_cast<std::uint16_t>(
                                readUintComponent(jb, jA.componentType, c));
                        }
                    }
                    if (wb) {
                        for (int c = 0; c < 4; ++c) {
                            sv.weights[c] =
                                readFloatComponent(wb, wA.componentType,
                                                   wA.normalized, c);
                        }
                    }
                    out.skinned[baseVertex + v] = sv;
                }
            }

            // Indices.
            const int aIdx = asInt(findInObject(prim, "indices"), -1);
            if (aIdx >= 0) {
                decodeIndexAccessor(ctx, aIdx, baseVertex, out.mesh.indices);
            } else {
                // Flat triangle list.
                for (std::uint32_t v = 0; v < vertCount; ++v) {
                    out.mesh.indices.push_back(baseVertex + v);
                }
            }
            MeshSubmesh sm;
            sm.firstIndex = static_cast<std::uint32_t>(baseIndex);
            sm.indexCount = static_cast<std::uint32_t>(out.mesh.indices.size() - baseIndex);
            sm.materialIndex = static_cast<std::uint32_t>(
                asInt(findInObject(prim, "material"), -1));
            out.mesh.submeshes.push_back(std::move(sm));
        }

        // AABB.
        if (!out.mesh.vertices.empty()) {
            constexpr float kInf = std::numeric_limits<float>::infinity();
            Aabb aabb{};
            aabb.min[0] = aabb.min[1] = aabb.min[2] =  kInf;
            aabb.max[0] = aabb.max[1] = aabb.max[2] = -kInf;
            for (const auto& v : out.mesh.vertices) {
                for (int k = 0; k < 3; ++k) {
                    if (v.position[k] < aabb.min[k]) aabb.min[k] = v.position[k];
                    if (v.position[k] > aabb.max[k]) aabb.max[k] = v.position[k];
                }
            }
            out.mesh.aabb = aabb;
        }
        scene.meshes.push_back(std::move(out));
    }

    // -- animations ------------------------------------------------------
    for (const auto& anim : asArrayOrEmpty(root.find("animations"))) {
        AnimationData out;
        out.name = asString(findInObject(anim, "name"));
        const auto& samplers = asArrayOrEmpty(findInObject(anim, "samplers"));
        const auto& channels = asArrayOrEmpty(findInObject(anim, "channels"));
        for (const auto& ch : channels) {
            AnimChannel c;
            const int samplerIdx = asInt(findInObject(ch, "sampler"), -1);
            if (samplerIdx < 0 ||
                samplerIdx >= static_cast<int>(samplers.size())) continue;
            const auto& sampler = samplers[static_cast<std::size_t>(samplerIdx)];
            const JsonValue* target = findInObject(ch, "target");
            if (!target) continue;
            const int targetNode = asInt(findInObject(*target, "node"), -1);
            // Map target node → joint index inside the first skin that
            // names this node. Multi-skin scenes resolve per-joint
            // ambiguity by first-match — fine for a v1 loader.
            int jointIdx = -1;
            for (const auto& sk : skinArr) {
                int jIdx = 0;
                for (const auto& j : asArrayOrEmpty(findInObject(sk, "joints"))) {
                    if (j.isNumber() && static_cast<int>(j.asNumber()) == targetNode) {
                        jointIdx = jIdx;
                        break;
                    }
                    ++jIdx;
                }
                if (jointIdx >= 0) break;
            }
            if (jointIdx < 0) continue;
            c.jointIndex = static_cast<std::uint32_t>(jointIdx);

            const std::string path = asString(findInObject(*target, "path"));
            if      (path == "translation") c.path = AnimChannelPath::Translation;
            else if (path == "rotation")    c.path = AnimChannelPath::Rotation;
            else if (path == "scale")       c.path = AnimChannelPath::Scale;
            else if (path == "weights")     c.path = AnimChannelPath::Weights;
            else continue;

            const std::string interp = asString(findInObject(sampler, "interpolation"));
            if      (interp == "STEP")        c.interpolation = AnimInterpolation::Step;
            else if (interp == "CUBICSPLINE") c.interpolation = AnimInterpolation::CubicSpline;
            else                              c.interpolation = AnimInterpolation::Linear;

            const int inputAcc  = asInt(findInObject(sampler, "input"), -1);
            const int outputAcc = asInt(findInObject(sampler, "output"), -1);
            decodeFloatAccessor(ctx, inputAcc, 1, c.inputs);
            const int outComp = (c.path == AnimChannelPath::Rotation) ? 4 : 3;
            decodeFloatAccessor(ctx, outputAcc, outComp, c.outputs);
            if (!c.inputs.empty()) {
                const float t = c.inputs.back();
                if (t > out.duration) out.duration = t;
            }
            out.channels.push_back(std::move(c));
        }
        scene.animations.push_back(std::move(out));
    }

    return AssetResult<GltfSceneData>::success(std::move(scene));
}

} // namespace

AssetResult<GltfSceneData> parseGltfScene(std::span<const std::byte> bytes,
                                          std::string_view sourcePath) {
    return parseScene(bytes, sourcePath);
}

AssetResult<GltfSceneData> loadGltfScene(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<GltfSceneData>::failure(bytes.code,
                                                   std::move(bytes.message));
    }
    return parseScene(bytes.value, path);
}

// Static-mesh shortcut: collapse every mesh into one merged MeshData.
AssetResult<MeshData> parseGltfMesh(std::span<const std::byte> bytes,
                                    std::string_view sourcePath) {
    auto sceneR = parseScene(bytes, sourcePath);
    if (!sceneR.ok()) {
        return AssetResult<MeshData>::failure(sceneR.code, std::move(sceneR.message));
    }
    MeshData merged;
    merged.sourcePath = std::string(sourcePath);
    for (auto& m : sceneR.value.meshes) {
        const auto baseVertex = static_cast<std::uint32_t>(merged.vertices.size());
        const auto baseIndex  = merged.indices.size();
        merged.vertices.insert(merged.vertices.end(),
                               m.mesh.vertices.begin(), m.mesh.vertices.end());
        for (auto idx : m.mesh.indices) merged.indices.push_back(baseVertex + idx);
        for (auto sm : m.mesh.submeshes) {
            sm.firstIndex += static_cast<std::uint32_t>(baseIndex);
            merged.submeshes.push_back(sm);
        }
    }
    if (!merged.vertices.empty()) {
        constexpr float kInf = std::numeric_limits<float>::infinity();
        Aabb aabb{};
        aabb.min[0] = aabb.min[1] = aabb.min[2] =  kInf;
        aabb.max[0] = aabb.max[1] = aabb.max[2] = -kInf;
        for (const auto& v : merged.vertices) {
            for (int k = 0; k < 3; ++k) {
                if (v.position[k] < aabb.min[k]) aabb.min[k] = v.position[k];
                if (v.position[k] > aabb.max[k]) aabb.max[k] = v.position[k];
            }
        }
        merged.aabb = aabb;
    }
    return AssetResult<MeshData>::success(std::move(merged));
}

AssetResult<MeshData> loadGltfMesh(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<MeshData>::failure(bytes.code,
                                              std::move(bytes.message));
    }
    return parseGltfMesh(bytes.value, path);
}

} // namespace threadmaxx::assets
