// §3.11.7b.5 batch 9b.4.c — Procedural 2-bone skinned mesh
// generator for the rpg_demo. Demonstrates the skinned pipeline
// end-to-end without dragging in a glTF parser; glTF stays as a
// v1.x candidate per FUTURE_WORK.
//
// The mesh is a vertical "stick figure" — 3 horizontal vertex
// rings at y={0, 1, 2}, 4 verts per ring (NESW around the y axis).
// The bottom ring (y=0) is weighted 1.0 to bone 0 (root), the
// middle ring is split 0.5/0.5 between bone 0 and bone 1, and the
// top ring (y=2) is weighted 1.0 to bone 1 (tip). 12 vertices, 8
// quads (16 triangles) connecting the rings vertically.
//
// Vertex layout matches the skinned pipeline's 56-byte stride:
//   pos[3]f + normal[3]f + boneIDs[4]u32 + boneWeights[4]f.

#pragma once

#include <cstdint>
#include <vector>

namespace rpg {

struct SkinnedCapsuleData {
    /// 14 floats per vertex: pos + normal + boneIDs (cast through
    /// uint32 view) + boneWeights. Total 12 verts × 14 floats = 168
    /// floats = 672 bytes.
    std::vector<float>         vertices;
    std::vector<std::uint16_t> indices;   // 16 quads * 6 = 96 triangle indices
};

/// Build the procedural 2-bone skinned capsule mesh. Pure function;
/// no allocation beyond the result vectors.
SkinnedCapsuleData makeSkinnedCapsule();

} // namespace rpg
