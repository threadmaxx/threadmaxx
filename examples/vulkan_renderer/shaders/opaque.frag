#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;       // xyz + 0
    vec4 cameraPos;      // xyz + 0
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(-pc.lightDir.xyz);
    float lambert = max(dot(n, l), 0.0);
    vec3 albedo = vColor;
    vec3 ambient = albedo * 0.18;
    vec3 lit = ambient + albedo * lambert * 0.82;
    outColor = vec4(lit, 1.0);
}
