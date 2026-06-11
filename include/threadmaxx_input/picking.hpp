#pragma once

namespace threadmaxx::input {

// Renderer-neutral camera POD. Matrices are column-major (Vulkan / GLSL
// convention). The projection MUST be Vulkan-style — NDC z ∈ [0, 1], not
// the OpenGL [-1, 1] range — to round-trip cleanly with the engine's
// rendering math.
struct Camera {
    float view[16]{};
    float projection[16]{};
    float viewportX{};
    float viewportY{};
    float viewportW{};
    float viewportH{};
};

struct Ray {
    float origin[3]{};
    float direction[3]{};  // unit length
};

struct ScreenPoint {
    float x{};
    float y{};
    bool inFrontOfCamera{};
};

// Constructs a world-space ray from a screen-space coordinate. The
// returned direction is unit length. Numerically robust at far-z thanks
// to the explicit normalize at the end (the perspective divide can blow
// up close to the far plane).
//
// Pre: camera viewport has positive area; projection is invertible.
Ray screenToRay(const Camera& cam, float screenX, float screenY) noexcept;

// Projects a world-space point onto the camera's viewport. `inFrontOfCamera`
// is false when the point is behind the camera (clip-space w <= 0) — the
// returned x/y are undefined in that case.
ScreenPoint worldToScreen(const Camera& cam, const float worldXyz[3]) noexcept;

}  // namespace threadmaxx::input
