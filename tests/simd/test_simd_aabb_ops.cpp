// threadmaxx_simd — scalar AABB + frustum kernel tests.
//
// Verifies:
//   1. `transform_aabb` with identity preserves the AABB.
//   2. `transform_aabb` with pure translation shifts min + max equally.
//   3. `transform_aabb` with rotation grows the AABB (a unit cube
//      rotated 45° around Y inflates from extent 1 → sqrt(2)).
//   4. `transform_aabb` with uniform scale scales min + max uniformly.
//   5. `frustum_cull` reports 1 for spheres clearly inside the frustum
//      and 0 for spheres clearly outside.
//   6. `frustum_cull` reports 1 for spheres straddling a plane
//      (radius extends past it).
//   7. Empty + mismatched span cases.

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/render/Camera.hpp>
#include <threadmaxx/render/Visibility.hpp>
#include <threadmaxx_simd/aabb_ops.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

bool approxEq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool approxEq(const threadmaxx::Vec3& a, const threadmaxx::Vec3& b,
              float eps = 1e-5f) {
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) &&
           approxEq(a.z, b.z, eps);
}

threadmaxx::Quat quatFromAxisAngle(float ax, float ay, float az, float angle) {
    const float h = 0.5f * angle;
    const float s = std::sin(h);
    return threadmaxx::Quat{ax * s, ay * s, az * s, std::cos(h)};
}

// Build a frustum manually from 6 unit-normal planes. Useful for
// tests because `extractFrustum(camera)` requires a full
// proj * view matrix; building axis-aligned planes is simpler.
threadmaxx::Frustum axisAlignedFrustum(float halfExtent) {
    threadmaxx::Frustum f;
    // Each plane: (a, b, c, d) where the half-space is ax+by+cz+d >= 0
    // (point inside). For a box centered at origin of half-extent h:
    //   left:   +x   - (-h) >= 0  → ( 1, 0, 0,  h)
    //   right: -x    -   h  >= 0  → (-1, 0, 0,  h)
    //   bottom:+y    - (-h) >= 0  → ( 0, 1, 0,  h)
    //   top:   -y    -   h  >= 0  → ( 0,-1, 0,  h)
    //   near:  +z    - (-h) >= 0  → ( 0, 0, 1,  h)
    //   far:   -z    -   h  >= 0  → ( 0, 0,-1,  h)
    const float h = halfExtent;
    f.planes[0] = { 1, 0, 0, h};
    f.planes[1] = {-1, 0, 0, h};
    f.planes[2] = { 0, 1, 0, h};
    f.planes[3] = { 0,-1, 0, h};
    f.planes[4] = { 0, 0, 1, h};
    f.planes[5] = { 0, 0,-1, h};
    return f;
}

} // namespace

int main() {
    using threadmaxx::Vec3;
    using threadmaxx::BoundingVolume;
    using threadmaxx::Transform;
    namespace simd = threadmaxx::simd;

    // ---- 1. transform_aabb identity -------------------------------------
    {
        std::vector<Transform> t = {Transform{}};
        std::vector<BoundingVolume> in = {{Vec3{-1, -1, -1}, Vec3{1, 1, 1}}};
        std::vector<BoundingVolume> out(1);
        simd::transform_aabb(t, in, out);
        CHECK(approxEq(out[0].min, Vec3{-1, -1, -1}));
        CHECK(approxEq(out[0].max, Vec3{ 1,  1,  1}));
        std::printf("[simd_aabb] identity OK\n");
    }

    // ---- 2. transform_aabb pure translation -----------------------------
    {
        Transform tr{};
        tr.position = Vec3{10, 20, 30};
        std::vector<Transform> t = {tr};
        std::vector<BoundingVolume> in = {{Vec3{-1, -1, -1}, Vec3{1, 1, 1}}};
        std::vector<BoundingVolume> out(1);
        simd::transform_aabb(t, in, out);
        CHECK(approxEq(out[0].min, Vec3{ 9, 19, 29}));
        CHECK(approxEq(out[0].max, Vec3{11, 21, 31}));
        std::printf("[simd_aabb] translation OK\n");
    }

    // ---- 3. transform_aabb 45° rotation around Y inflates ---------------
    {
        // A unit cube spanning [-0.5, 0.5] rotated 45° around Y. The
        // X+Z extents grow from 1.0 to sqrt(2) ≈ 1.41421; Y stays 1.0.
        Transform tr{};
        tr.orientation = quatFromAxisAngle(0, 1, 0, 3.14159265f / 4.0f);
        std::vector<Transform> t = {tr};
        std::vector<BoundingVolume> in = {{Vec3{-0.5f, -0.5f, -0.5f},
                                           Vec3{ 0.5f,  0.5f,  0.5f}}};
        std::vector<BoundingVolume> out(1);
        simd::transform_aabb(t, in, out);
        const float halfDiag = 0.5f * std::sqrt(2.0f);
        CHECK(approxEq(out[0].min.x, -halfDiag, 1e-5f));
        CHECK(approxEq(out[0].max.x,  halfDiag, 1e-5f));
        CHECK(approxEq(out[0].min.y, -0.5f, 1e-5f));
        CHECK(approxEq(out[0].max.y,  0.5f, 1e-5f));
        CHECK(approxEq(out[0].min.z, -halfDiag, 1e-5f));
        CHECK(approxEq(out[0].max.z,  halfDiag, 1e-5f));
        std::printf("[simd_aabb] 45deg-Y inflation OK\n");
    }

    // ---- 4. transform_aabb uniform scale --------------------------------
    {
        Transform tr{};
        tr.scale = Vec3{2, 3, 4};
        std::vector<Transform> t = {tr};
        std::vector<BoundingVolume> in = {{Vec3{-1, -1, -1}, Vec3{1, 1, 1}}};
        std::vector<BoundingVolume> out(1);
        simd::transform_aabb(t, in, out);
        CHECK(approxEq(out[0].min, Vec3{-2, -3, -4}));
        CHECK(approxEq(out[0].max, Vec3{ 2,  3,  4}));
        std::printf("[simd_aabb] non-uniform scale OK\n");
    }

    // ---- 5. frustum_cull — inside / outside cases ----------------------
    {
        const auto fr = axisAlignedFrustum(10.0f);
        std::vector<Vec3>  centers = {
            {0, 0, 0},              // dead center → inside
            {100, 0, 0},            // far outside +X → outside
            {-100, 0, 0},           // far outside -X → outside
            {0, 100, 0},            // far outside +Y
            {0, 0, 100},            // far outside +Z
        };
        std::vector<float> radii   = {1.0f, 0.5f, 0.5f, 0.5f, 0.5f};
        std::vector<std::uint8_t> mask(centers.size(), 0xFF);  // sentinel
        simd::frustum_cull(centers, radii, fr, mask);
        CHECK_EQ(mask[0], std::uint8_t{1});
        CHECK_EQ(mask[1], std::uint8_t{0});
        CHECK_EQ(mask[2], std::uint8_t{0});
        CHECK_EQ(mask[3], std::uint8_t{0});
        CHECK_EQ(mask[4], std::uint8_t{0});
        std::printf("[simd_aabb] frustum_cull in/out OK\n");
    }

    // ---- 6. frustum_cull — sphere straddling a plane -------------------
    {
        const auto fr = axisAlignedFrustum(10.0f);
        // Center at (12, 0, 0) — 2 units outside +X plane. Radius 3
        // means the sphere extends back to x=9, which IS inside.
        std::vector<Vec3>  centers = {{12, 0, 0}, {12, 0, 0}};
        std::vector<float> radii   = {3.0f, 1.0f};  // straddling, fully out
        std::vector<std::uint8_t> mask(2, 0xFF);
        simd::frustum_cull(centers, radii, fr, mask);
        CHECK_EQ(mask[0], std::uint8_t{1});  // straddling → visible
        CHECK_EQ(mask[1], std::uint8_t{0});  // fully out → culled
        std::printf("[simd_aabb] frustum_cull straddling OK\n");
    }

    // ---- 7. empty + mismatched -----------------------------------------
    {
        const auto fr = axisAlignedFrustum(10.0f);
        std::vector<Vec3>  emptyC;
        std::vector<float> emptyR;
        std::vector<std::uint8_t> emptyM;
        simd::frustum_cull(emptyC, emptyR, fr, emptyM);  // no-op
        std::vector<BoundingVolume> emptyBV;
        std::vector<Transform>      emptyT;
        simd::transform_aabb(emptyT, emptyBV, emptyBV);   // no-op

        // Mismatched: visible_mask shorter than centers → stops early.
        std::vector<Vec3>  centers = {{0,0,0}, {0,0,0}, {0,0,0}};
        std::vector<float> radii   = {1, 1, 1};
        std::vector<std::uint8_t> mask = {0xFF, 0xFF};  // length 2
        simd::frustum_cull(centers, radii, fr, mask);
        // Only the first 2 mask entries were updated.
        CHECK_EQ(mask[0], std::uint8_t{1});
        CHECK_EQ(mask[1], std::uint8_t{1});
        std::printf("[simd_aabb] empty + mismatched OK\n");
    }

    EXIT_WITH_RESULT();
}
