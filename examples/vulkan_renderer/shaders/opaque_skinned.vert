#version 450

// §3.11.7b.5 batch 9b.4.a — Skinned-mesh vertex shader.
//
// Vertex layout (binding 0, 56 bytes stride):
//   location 0  vec3   inPosition
//   location 1  vec3   inNormal
//   location 8  uvec4  inBoneIDs        // up to 4 bones per vertex
//   location 9  vec4   inBoneWeights    // matching weights (Σ ≈ 1)
//
// Per-instance binding (binding 1) is the same `InstanceLayoutEntry`
// as the non-skinned opaque pipeline (locations 2-7); the `poseSlot`
// field in `instMeshMat.w` is reinterpreted here as the bone-base
// offset into the bone matrix array.
//
// Bone storage (descriptor set 0, binding 0): SSBO of `mat4` bone
// transforms. The renderer (9b.4.b, deferred) uploads this per-frame
// via the `UploadRing`; for 9b.4.a the pipeline exists but is never
// bound to a draw, so the descriptor set never needs to be populated.

// Per-vertex
layout(location = 0) in vec3  inPosition;
layout(location = 1) in vec3  inNormal;
layout(location = 8) in uvec4 inBoneIDs;
layout(location = 9) in vec4  inBoneWeights;

// Per-instance — same layout as the unskinned opaque pipeline.
layout(location = 2)  in vec4  instPos;
layout(location = 3)  in vec4  instOrientation;
layout(location = 4)  in vec4  instScale;
layout(location = 5)  in vec4  instMatOverride;
layout(location = 6)  in ivec4 instMeshMat;     // .w = bone-base offset
layout(location = 7)  in uvec4 instFlagsSort;

// Bone matrices. Indexed as `boneMatrices[boneBase + boneID]`.
layout(set = 0, binding = 0) readonly buffer BoneMatrices {
    mat4 boneMatrices[];
};

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;
    vec4 cameraPos;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

vec3 rotateByQuat(vec4 q, vec3 v) {
    vec3 u = q.xyz;
    float s = q.w;
    return 2.0 * dot(u, v) * u
         + (s * s - dot(u, u)) * v
         + 2.0 * s * cross(u, v);
}

void main() {
    int boneBase = instMeshMat.w;

    // Blend up to 4 bone matrices weighted by the per-vertex weights.
    // The renderer guarantees Σ weights ≈ 1.0; we trust that here
    // rather than re-normalize. Zero-weight slots multiply against
    // bone 0 of the skeleton (the root or a sentinel identity),
    // and since their weight is 0 they contribute nothing.
    mat4 skin =
        inBoneWeights.x * boneMatrices[boneBase + int(inBoneIDs.x)] +
        inBoneWeights.y * boneMatrices[boneBase + int(inBoneIDs.y)] +
        inBoneWeights.z * boneMatrices[boneBase + int(inBoneIDs.z)] +
        inBoneWeights.w * boneMatrices[boneBase + int(inBoneIDs.w)];

    vec3 skinnedPos    = (skin * vec4(inPosition, 1.0)).xyz;
    vec3 skinnedNormal = mat3(skin) * inNormal;

    // After skinning, apply the per-instance transform (same as the
    // non-skinned opaque path). For most setups the per-instance
    // transform is identity for skinned meshes (the skeleton root
    // carries the world transform via the bones themselves), but
    // leaving it composable means a character can be parented to a
    // moving platform without changing the skeleton math.
    vec3 scaledPos = skinnedPos * instScale.xyz;
    vec3 worldPos  = rotateByQuat(instOrientation, scaledPos) + instPos.xyz;

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    vNormal = normalize(rotateByQuat(instOrientation, skinnedNormal));
    vColor  = instMatOverride.rgb;
}
