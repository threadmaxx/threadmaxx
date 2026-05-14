#pragma once

#include "../Components.hpp"  // Vec3, BoundingVolume
#include "Camera.hpp"
#include "DrawItem.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace threadmaxx {

/// Standard 6-plane frustum extracted from a `projection * view` matrix.
/// Each plane is `(a, b, c, d)` such that points satisfying
/// `ax + by + cz + d >= 0` are inside the half-space.
struct Frustum {
    std::array<std::array<float, 4>, 6> planes = {};
};

namespace render_detail {

/// Compute `clip = proj * view` from a @ref Camera. Matrices are
/// column-major in the convention shaders consume directly.
inline std::array<float, 16> cameraClipMatrix(const Camera& c) noexcept {
    const auto& a = c.projection;  // proj
    const auto& b = c.view;        // view
    std::array<float, 16> r = {};
    // Column-major multiply: r = a * b. Element r[col*4 + row].
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[static_cast<std::size_t>(k * 4 + row)] *
                       b[static_cast<std::size_t>(col * 4 + k)];
            }
            r[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return r;
}

} // namespace render_detail

/// Extract the 6 frustum planes from a @ref Camera.
///
/// Planes are normalized so the AABB test below can use a simple
/// p-vertex selection without further math. Conservative — the test
/// admits a few false positives near the corners, never false
/// negatives.
inline Frustum extractFrustum(const Camera& camera) noexcept {
    const auto m = render_detail::cameraClipMatrix(camera);
    Frustum f;
    auto setPlane = [&](std::size_t idx, float a, float b, float c, float d) {
        const float len2 = a * a + b * b + c * c;
        // Avoid 0/0 on degenerate matrices — leave the plane "always inside"
        // so the visibility test passes everything through rather than
        // accidentally hiding everything.
        if (len2 <= 0.0f) {
            f.planes[idx] = {0.0f, 0.0f, 0.0f, 1.0f};
            return;
        }
        const float inv = 1.0f / std::sqrt(len2);
        f.planes[idx] = {a * inv, b * inv, c * inv, d * inv};
    };
    // Row-from-column-major access: row i is m[0*4+i], m[1*4+i], m[2*4+i], m[3*4+i].
    auto row = [&](int i) {
        return std::array<float, 4>{
            m[static_cast<std::size_t>(0 * 4 + i)],
            m[static_cast<std::size_t>(1 * 4 + i)],
            m[static_cast<std::size_t>(2 * 4 + i)],
            m[static_cast<std::size_t>(3 * 4 + i)],
        };
    };
    const auto r0 = row(0);
    const auto r1 = row(1);
    const auto r2 = row(2);
    const auto r3 = row(3);
    setPlane(0, r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]);  // left
    setPlane(1, r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]);  // right
    setPlane(2, r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]);  // bottom
    setPlane(3, r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]);  // top
    setPlane(4, r3[0] + r2[0], r3[1] + r2[1], r3[2] + r2[2], r3[3] + r2[3]);  // near
    setPlane(5, r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]);  // far
    return f;
}

/// True iff the world-space AABB `[min, max]` is at least partially
/// inside @p f. Uses the p-vertex test: for each plane pick the AABB
/// corner farthest in the plane's positive direction; if even that
/// corner is on the negative side, the box is entirely outside.
inline bool intersectsFrustum(const Frustum& f,
                              const Vec3& min,
                              const Vec3& max) noexcept {
    for (const auto& p : f.planes) {
        const float px = p[0] > 0 ? max.x : min.x;
        const float py = p[1] > 0 ? max.y : min.y;
        const float pz = p[2] > 0 ? max.z : min.z;
        if (p[0] * px + p[1] * py + p[2] * pz + p[3] < 0.0f) return false;
    }
    return true;
}

/// Convenience: extract + test in one call.
inline bool intersectsFrustum(const Camera& camera,
                              const Vec3& min,
                              const Vec3& max) noexcept {
    return intersectsFrustum(extractFrustum(camera), min, max);
}

/// Per-camera frustum cull over a parallel pair of spans. Writes each
/// item's @ref DrawItem::cameraMask in place: bit `i` is set iff the
/// item's bounds intersect camera `cameras[i]`'s frustum.
///
/// @p drawItems and @p bounds must have the same length and be
/// parallel-indexed. Up to 32 cameras are supported (one bit per
/// camera in the mask); cameras beyond the 32nd are ignored.
inline void cullByFrustum(std::span<DrawItem> drawItems,
                          std::span<const BoundingVolume> bounds,
                          std::span<const Camera> cameras) noexcept {
    const std::size_t cameraCount =
        cameras.size() < 32 ? cameras.size() : 32;
    std::array<Frustum, 32> frusta;
    for (std::size_t c = 0; c < cameraCount; ++c) {
        frusta[c] = extractFrustum(cameras[c]);
    }
    const std::size_t n = drawItems.size() < bounds.size()
                              ? drawItems.size() : bounds.size();
    for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t mask = 0;
        for (std::size_t c = 0; c < cameraCount; ++c) {
            if (intersectsFrustum(frusta[c], bounds[i].min, bounds[i].max)) {
                mask |= (1u << c);
            }
        }
        drawItems[i].cameraMask = mask;
    }
}

} // namespace threadmaxx
