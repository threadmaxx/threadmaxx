// Hand-builds a minimal .glb in memory (no external file) and parses it.
// Covers: header magic/version, JSON chunk parse, BIN chunk decode,
// VEC3 POSITION + NORMAL + TEXCOORD_0 accessors, SCALAR UINT16 indices,
// skin with two joints + inverse bind matrices, one animation channel.

#include "Check.hpp"

#include "threadmaxx_assets/loaders/gltf.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kGlbMagic   = 0x46546C67u;
constexpr std::uint32_t kGlbVersion = 2u;
constexpr std::uint32_t kJsonChunk  = 0x4E4F534Au;
constexpr std::uint32_t kBinChunk   = 0x004E4942u;

void appendU32(std::vector<std::byte>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(std::byte{static_cast<unsigned char>((v >> (i * 8)) & 0xFFu)});
    }
}

void appendBytes(std::vector<std::byte>& out, const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    for (std::size_t i = 0; i < n; ++i) out.push_back(b[i]);
}

void padTo4(std::vector<std::byte>& v, std::byte fill) {
    while (v.size() % 4 != 0) v.push_back(fill);
}

} // namespace

int main() {
    using namespace threadmaxx::assets;

    // -- BIN payload: positions, normals, uvs, indices, IBM matrices,
    //                 anim input (time), anim output (translation track) --
    std::vector<std::byte> bin;

    // 3 vertices forming one triangle.
    const float positions[9] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    const float normals[9] = {
        0, 0, 1,
        0, 0, 1,
        0, 0, 1,
    };
    const float uvs[6] = {
        0, 0,
        1, 0,
        0, 1,
    };
    const std::uint16_t indices[3] = {0, 1, 2};

    const std::size_t offPositions = bin.size();
    appendBytes(bin, positions, sizeof(positions));
    const std::size_t offNormals   = bin.size();
    appendBytes(bin, normals,   sizeof(normals));
    const std::size_t offUvs      = bin.size();
    appendBytes(bin, uvs,       sizeof(uvs));
    const std::size_t offIndices  = bin.size();
    appendBytes(bin, indices,   sizeof(indices));
    padTo4(bin, std::byte{0});

    // Two inverse-bind 4x4 identity matrices (one per joint).
    const std::size_t offIbm = bin.size();
    for (int j = 0; j < 2; ++j) {
        float m[16] = {1, 0, 0, 0,
                       0, 1, 0, 0,
                       0, 0, 1, 0,
                       0, 0, 0, 1};
        appendBytes(bin, m, sizeof(m));
    }

    // Animation: 2 keyframes at t=0, t=1. Output translates joint 0
    // from (0,0,0) → (1,0,0).
    const std::size_t offAnimInput = bin.size();
    const float animInputs[2] = {0.0f, 1.0f};
    appendBytes(bin, animInputs, sizeof(animInputs));
    const std::size_t offAnimOutput = bin.size();
    const float animOutputs[6] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
    };
    appendBytes(bin, animOutputs, sizeof(animOutputs));
    padTo4(bin, std::byte{0});

    // -- JSON descriptor --
    std::string json =
        "{"
        "\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0,1]}],"
        // node 0 owns the mesh + skin; node 1 is the root joint.
        // node 2 is a child joint.
        "\"nodes\":["
          "{\"mesh\":0,\"skin\":0},"
          "{\"children\":[2],\"translation\":[0,0,0]},"
          "{\"translation\":[0,1,0]}"
        "],"
        "\"meshes\":[{"
          "\"primitives\":[{"
            "\"attributes\":{"
              "\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2"
            "},"
            "\"indices\":3"
          "}]"
        "}],"
        "\"skins\":[{"
          "\"joints\":[1,2],"
          "\"inverseBindMatrices\":4"
        "}],"
        "\"animations\":[{"
          "\"name\":\"walk\","
          "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}],"
          "\"samplers\":[{\"input\":5,\"output\":6,\"interpolation\":\"LINEAR\"}]"
        "}],"
        "\"buffers\":[{\"byteLength\":";
    json += std::to_string(bin.size());
    json += "}],";
    json += "\"bufferViews\":[";
    auto bv = [&](std::size_t off, std::size_t len) {
        json += "{\"buffer\":0,\"byteOffset\":";
        json += std::to_string(off);
        json += ",\"byteLength\":";
        json += std::to_string(len);
        json += "}";
    };
    bv(offPositions,    sizeof(positions));        json += ",";
    bv(offNormals,      sizeof(normals));          json += ",";
    bv(offUvs,          sizeof(uvs));              json += ",";
    bv(offIndices,      sizeof(indices));          json += ",";
    bv(offIbm,          sizeof(float) * 32);       json += ",";
    bv(offAnimInput,    sizeof(animInputs));       json += ",";
    bv(offAnimOutput,   sizeof(animOutputs));
    json += "],";
    json +=
        "\"accessors\":["
          "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"   // 0 POSITION
          "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"   // 1 NORMAL
          "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"   // 2 TEXCOORD_0
          "{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}," // 3 indices (UINT16)
          "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"   // 4 IBM
          "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}," // 5 anim input
          "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}"   // 6 anim output
        "]}";

    // Pad JSON to 4 bytes with spaces.
    while (json.size() % 4 != 0) json.push_back(' ');

    // -- assemble .glb --
    std::vector<std::byte> glb;
    appendU32(glb, kGlbMagic);
    appendU32(glb, kGlbVersion);
    // total length: header (12) + JSON chunk header (8) + json + BIN chunk header (8) + bin
    const std::uint32_t total = static_cast<std::uint32_t>(
        12 + 8 + json.size() + 8 + bin.size());
    appendU32(glb, total);
    appendU32(glb, static_cast<std::uint32_t>(json.size()));
    appendU32(glb, kJsonChunk);
    appendBytes(glb, json.data(), json.size());
    appendU32(glb, static_cast<std::uint32_t>(bin.size()));
    appendU32(glb, kBinChunk);
    appendBytes(glb, bin.data(), bin.size());

    // -- parse scene --
    auto sceneR = parseGltfScene(std::span<const std::byte>(glb.data(), glb.size()),
                                 "<inline>");
    CHECK(sceneR.ok());
    if (!sceneR.ok()) {
        EXIT_WITH_RESULT();
    }
    const auto& scene = sceneR.value;
    CHECK_EQ(scene.meshes.size(),     std::size_t{1});
    CHECK_EQ(scene.skins.size(),      std::size_t{1});
    CHECK_EQ(scene.animations.size(), std::size_t{1});

    // Mesh: 3 vertices, 3 indices, 1 submesh.
    const auto& mesh = scene.meshes[0].mesh;
    CHECK_EQ(mesh.vertices.size(),  std::size_t{3});
    CHECK_EQ(mesh.indices.size(),   std::size_t{3});
    CHECK_EQ(mesh.submeshes.size(), std::size_t{1});

    // Skin: 2 joints, name + parent + IBM populated.
    const auto& skin = scene.skins[0];
    CHECK_EQ(skin.joints.size(), std::size_t{2});
    CHECK_EQ(skin.joints[0].parent, -1);
    // Joint 1 is a child of node 1; node 1 is joint 0 in this skin.
    CHECK_EQ(skin.joints[1].parent, 0);
    CHECK(std::abs(skin.joints[0].inverseBind[0] - 1.0f) < 1e-5f);
    CHECK(std::abs(skin.joints[1].inverseBind[5] - 1.0f) < 1e-5f);

    // Animation: 1 channel on joint 0, translation, 2 keyframes,
    // duration = 1.0.
    const auto& anim = scene.animations[0];
    CHECK_EQ(anim.channels.size(), std::size_t{1});
    CHECK(std::abs(anim.duration - 1.0f) < 1e-5f);
    const auto& ch = anim.channels[0];
    CHECK_EQ(ch.jointIndex, std::uint32_t{0});
    CHECK(ch.path == AnimChannelPath::Translation);
    CHECK(ch.interpolation == AnimInterpolation::Linear);
    CHECK_EQ(ch.inputs.size(),  std::size_t{2});
    CHECK_EQ(ch.outputs.size(), std::size_t{6});
    CHECK(std::abs(ch.outputs[3] - 1.0f) < 1e-5f);  // second keyframe x

    // Static-mesh shortcut returns the same vertex/index totals merged.
    auto meshR = parseGltfMesh(std::span<const std::byte>(glb.data(), glb.size()),
                               "<inline>");
    CHECK(meshR.ok());
    CHECK_EQ(meshR.value.vertices.size(), std::size_t{3});
    CHECK_EQ(meshR.value.indices.size(),  std::size_t{3});

    // Malformed input rejected.
    std::vector<std::byte> bogus(8, std::byte{0});
    auto bad = parseGltfScene(std::span<const std::byte>(bogus.data(), bogus.size()),
                              "<inline>");
    CHECK(!bad.ok());
    CHECK(bad.code == ErrorCode::BadMagic);

    EXIT_WITH_RESULT();
}
