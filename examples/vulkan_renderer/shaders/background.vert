#version 450

// World-space background quad. The two triangles are derived from
// gl_VertexIndex 0..5. Vertex positions are placed at +/- worldHalfExtent
// in X/Y at world z = -1.0 (slightly behind the gameplay plane at z=0)
// then transformed by the host-pushed viewProj so the JPG appears
// anchored to the world — moving with the camera rather than fixed to
// the screen.
//
// stb_image decodes JPEGs with row 0 at the top of the image. The
// world's +Y axis points up, so c.y = +1 (world top) samples v = 0
// (image top row).

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 worldRect;   // x=centerX, y=centerY, z=halfW, w=halfH
} pc;

layout(location = 0) out vec2 vUv;

const vec2 kCorners[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(+1.0, -1.0), vec2(-1.0, +1.0),
    vec2(-1.0, +1.0), vec2(+1.0, -1.0), vec2(+1.0, +1.0)
);

void main() {
    vec2 c = kCorners[gl_VertexIndex];
    vec3 worldPos = vec3(pc.worldRect.x + c.x * pc.worldRect.z,
                         pc.worldRect.y + c.y * pc.worldRect.w,
                         -1.0);
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    vUv = vec2((c.x + 1.0) * 0.5, (1.0 - c.y) * 0.5);
}
