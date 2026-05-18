/// @file SkinnedCapsule.cpp
/// §3.11.7b.5 batch 9b.4.c — procedural 2-bone skinned mesh.
///
/// Ring layout (looking down +Y):
///        N (0,_,-1)
///         •
///   W •———+———• E
///         |
///        S (0,_,+1)
///
/// Three rings stacked vertically at y = 0, 1, 2. The four ring
/// vertices are connected to the next ring's matching cardinal
/// vertices with two triangles per quad, giving 8 quads × 2 = 16
/// triangles total. Normals are radial (outward in the XZ plane).
///
/// Bone weighting:
///   Ring 0 (y=0):  bone 0 only            (weight {1,0,0,0})
///   Ring 1 (y=1):  bone 0 + bone 1 half   (weight {0.5,0.5,0,0})
///   Ring 2 (y=2):  bone 1 only            (weight {0,1,0,0})
///
/// For the boneIDs slots that have weight=0, we point them at bone
/// 0 (the root) to avoid out-of-range lookups in the shader's
/// fixed 4-bone blend.

#include "SkinnedCapsule.hpp"

#include <cstring>

namespace rpg {

namespace {

// 14 floats per vertex matching the skinned pipeline's stride.
// `boneIDs` are uint32, but we store them in the float vector via
// memcpy so the layout stays a flat float[14].
struct Vert {
    float        pos[3];
    float        normal[3];
    std::uint32_t boneIDs[4];
    float        boneWeights[4];
};
static_assert(sizeof(Vert) == 56,
              "Vert must match the skinned pipeline's 56-byte stride.");

void pushVert(std::vector<float>& out, const Vert& v) {
    // Append the 14-float record. Cast through memcpy to preserve
    // the uint32_t bit pattern in the boneIDs lanes.
    const std::size_t base = out.size();
    out.resize(base + 14);
    std::memcpy(out.data() + base, &v, sizeof(Vert));
}

} // namespace

SkinnedCapsuleData makeSkinnedCapsule() {
    SkinnedCapsuleData mesh;
    mesh.vertices.reserve(12 * 14);
    mesh.indices.reserve(16 * 3);

    // Ring 0 (y=0): 4 verts, all weighted to bone 0.
    pushVert(mesh.vertices, Vert{{ 0.5f, 0.0f,  0.0f}, { 1, 0,  0}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{ 0.0f, 0.0f, -0.5f}, { 0, 0, -1}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{-0.5f, 0.0f,  0.0f}, {-1, 0,  0}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{ 0.0f, 0.0f,  0.5f}, { 0, 0,  1}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});
    // Ring 1 (y=1): mid, weighted 50/50.
    pushVert(mesh.vertices, Vert{{ 0.5f, 1.0f,  0.0f}, { 1, 0,  0}, {0, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{ 0.0f, 1.0f, -0.5f}, { 0, 0, -1}, {0, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{-0.5f, 1.0f,  0.0f}, {-1, 0,  0}, {0, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{ 0.0f, 1.0f,  0.5f}, { 0, 0,  1}, {0, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}});
    // Ring 2 (y=2): top, weighted entirely to bone 1.
    pushVert(mesh.vertices, Vert{{ 0.5f, 2.0f,  0.0f}, { 1, 0,  0}, {1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{ 0.0f, 2.0f, -0.5f}, { 0, 0, -1}, {1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{-0.5f, 2.0f,  0.0f}, {-1, 0,  0}, {1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});
    pushVert(mesh.vertices, Vert{{ 0.0f, 2.0f,  0.5f}, { 0, 0,  1}, {1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}});

    // Connect ring 0 to ring 1, then ring 1 to ring 2. Each ring
    // pairs into 4 quads; CCW winding from outside (matches the
    // pipeline's `VK_FRONT_FACE_COUNTER_CLOCKWISE`).
    auto pushQuad = [&](std::uint16_t a, std::uint16_t b,
                        std::uint16_t c, std::uint16_t d) {
        // a→b (bottom), c→d (top). CCW from outside: a, b, d, a, d, c.
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(d);
        mesh.indices.push_back(a);
        mesh.indices.push_back(d);
        mesh.indices.push_back(c);
    };

    // Lower band (ring 0 → ring 1).
    pushQuad(0, 1, 4, 5);
    pushQuad(1, 2, 5, 6);
    pushQuad(2, 3, 6, 7);
    pushQuad(3, 0, 7, 4);
    // Upper band (ring 1 → ring 2).
    pushQuad(4, 5,  8,  9);
    pushQuad(5, 6,  9, 10);
    pushQuad(6, 7, 10, 11);
    pushQuad(7, 4, 11,  8);

    return mesh;
}

} // namespace rpg
