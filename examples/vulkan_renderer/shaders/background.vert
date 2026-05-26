#version 450

// Fullscreen-triangle background pass. No vertex buffer is bound; we
// derive the three NDC corners + UVs from gl_VertexIndex 0..2.
//
// The renderer's default viewport flips Y (height < 0), so NDC y = +1
// lands at framebuffer y = 0 (top of screen). We pre-flip the V
// coordinate here so a stb_image-decoded JPEG (row 0 = top of image)
// samples as UV (0,0) at the top-left pixel.

layout(location = 0) out vec2 vUv;

void main() {
    vec2 uv = vec2(float((gl_VertexIndex << 1) & 2),
                   float(gl_VertexIndex & 2));      // (0,0), (2,0), (0,2)
    vec2 ndc = uv * 2.0 - vec2(1.0);                // (-1,-1), (3,-1), (-1,3)
    vUv = vec2(uv.x, 1.0 - uv.y);                   // image-space V (0..1)
    gl_Position = vec4(ndc, 1.0, 1.0);              // z = 1.0 — sits at far plane
}
