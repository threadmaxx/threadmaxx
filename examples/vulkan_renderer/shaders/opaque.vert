#version 450

// Per-vertex (unit-cube fallback mesh)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// Per-instance — laid out to match InstanceLayoutEntry (128 bytes / 8 vec4s).
layout(location = 2)  in vec4 instPos;           // xyz + pad
layout(location = 3)  in vec4 instOrientation;   // quat xyzw
layout(location = 4)  in vec4 instScale;         // xyz + pad
layout(location = 5)  in vec4 instMatOverride;   // tint rgba
layout(location = 6)  in ivec4 instMeshMat;      // mesh / material / skeleton / pose-slot
layout(location = 7)  in uvec4 instFlagsSort;    // flags / sortLow / sortHigh / entityIndex

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;       // xyz + 0
    vec4 cameraPos;      // xyz + 0
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
    vec3 scaledPos = inPosition * instScale.xyz;
    vec3 worldPos  = rotateByQuat(instOrientation, scaledPos) + instPos.xyz;

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    vNormal = normalize(rotateByQuat(instOrientation, inNormal));
    vColor  = instMatOverride.rgb;
}
