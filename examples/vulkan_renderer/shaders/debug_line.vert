#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;  // unpacked RGBA in 0..1

layout(push_constant) uniform Push {
    mat4 viewProj;
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    vColor = inColor;
}
