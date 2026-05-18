// threadmaxx_simd — extended AVX2 equivalence tests (S4).
//
// Verifies the scalar ↔ AVX2 equivalence for the S4-vectorized
// kernels:
//   - `quat_normalize` — AVX2 path uses `_mm256_dp_ps` for the
//     length-squared reduction; zero-norm input must collapse to
//     identity (0,0,0,1) on both paths.
//   - `frustum_cull` — AVX2 path vectorizes across 8 spheres,
//     packing 8 visibility lanes to 8 bytes via `movemask` + bit
//     spread.
//
// Same skip-on-no-AVX2 policy as `test_simd_vec3_avx2_equivalence`.

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/render/Visibility.hpp>
#include <threadmaxx_simd/config.hpp>
#include <threadmaxx_simd/detail/scalar.hpp>

#if THREADMAXX_SIMD_HAS_AVX2
#  include <threadmaxx_simd/detail/avx2.hpp>
#endif

#include <threadmaxx_simd/transform_ops.hpp>
#include <threadmaxx_simd/aabb_ops.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <vector>

namespace {

bool approxEq(float a, float b, float absEps, float relEps) {
    const float diff = std::fabs(a - b);
    if (diff <= absEps) return true;
    const float scale = std::max(std::fabs(a), std::fabs(b));
    return diff <= relEps * scale;
}

bool approxEq(const threadmaxx::Quat& a, const threadmaxx::Quat& b,
              float absEps = 1e-6f, float relEps = 1e-6f) {
    return approxEq(a.x, b.x, absEps, relEps) &&
           approxEq(a.y, b.y, absEps, relEps) &&
           approxEq(a.z, b.z, absEps, relEps) &&
           approxEq(a.w, b.w, absEps, relEps);
}

bool approxEq(const threadmaxx::Vec3& a, const threadmaxx::Vec3& b,
              float absEps = 1e-4f, float relEps = 1e-5f) {
    return approxEq(a.x, b.x, absEps, relEps) &&
           approxEq(a.y, b.y, absEps, relEps) &&
           approxEq(a.z, b.z, absEps, relEps);
}

bool approxEq(const threadmaxx::BoundingVolume& a,
              const threadmaxx::BoundingVolume& b,
              float absEps = 1e-4f, float relEps = 1e-5f) {
    return approxEq(a.min, b.min, absEps, relEps) &&
           approxEq(a.max, b.max, absEps, relEps);
}

threadmaxx::Frustum axisAlignedFrustum(float halfExtent) {
    threadmaxx::Frustum f;
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
#if !THREADMAXX_SIMD_HAS_AVX2
    std::printf("[simd_avx2_equivalence_extended] AVX2 not built; "
                "test skipped\n");
    return 0;
#else
    using threadmaxx::Quat;
    using threadmaxx::Vec3;
    using threadmaxx::Frustum;
    namespace scalar = threadmaxx::simd::detail::scalar;
    namespace avx2   = threadmaxx::simd::detail::avx2;

    std::mt19937 rng(0xDEADBEEF);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);

    // ---- 1. quat_normalize equivalence ---------------------------------
    {
        const std::vector<std::size_t> sizes = {
            0, 1, 2, 3, 4, 7, 8, 11, 16, 17, 31, 64, 257, 1024
        };
        for (std::size_t n : sizes) {
            std::vector<Quat> qScalar(n), qAvx2(n);
            for (std::size_t i = 0; i < n; ++i) {
                Quat q{dist(rng), dist(rng), dist(rng), dist(rng)};
                // Inject zero-norm quats at known positions to
                // exercise the identity-fallback path on both
                // implementations.
                if (n > 0 && (i == 0 || i == n / 2 || i + 1 == n)) {
                    q = Quat{0, 0, 0, 0};
                }
                qScalar[i] = q;
                qAvx2[i]   = q;
            }
            scalar::quat_normalize(std::span<Quat>(qScalar));
            avx2::quat_normalize  (std::span<Quat>(qAvx2));
            for (std::size_t i = 0; i < n; ++i) {
                if (!approxEq(qScalar[i], qAvx2[i])) {
                    std::fprintf(stderr,
                        "quat_normalize mismatch at %zu (n=%zu): "
                        "scalar=(%g,%g,%g,%g) avx2=(%g,%g,%g,%g)\n",
                        i, n,
                        qScalar[i].x, qScalar[i].y, qScalar[i].z, qScalar[i].w,
                        qAvx2[i].x,   qAvx2[i].y,   qAvx2[i].z,   qAvx2[i].w);
                    CHECK(false);
                }
            }
        }
        std::printf("[simd_avx2_equivalence_extended] quat_normalize OK\n");
    }

    // ---- 2. frustum_cull equivalence -----------------------------------
    {
        const Frustum fr = axisAlignedFrustum(10.0f);
        const std::vector<std::size_t> sizes = {
            0, 1, 7, 8, 9, 16, 17, 31, 64, 257, 1024
        };
        std::uniform_real_distribution<float> centerDist(-15.0f, 15.0f);
        std::uniform_real_distribution<float> radiusDist(0.1f, 3.0f);
        for (std::size_t n : sizes) {
            std::vector<Vec3>  centers(n);
            std::vector<float> radii(n);
            for (std::size_t i = 0; i < n; ++i) {
                centers[i] = Vec3{centerDist(rng), centerDist(rng), centerDist(rng)};
                radii[i]   = radiusDist(rng);
            }
            std::vector<std::uint8_t> maskScalar(n, 0xFF);
            std::vector<std::uint8_t> maskAvx2(n, 0xFF);
            scalar::frustum_cull(centers, radii, fr, maskScalar);
            avx2::frustum_cull  (centers, radii, fr, maskAvx2);
            for (std::size_t i = 0; i < n; ++i) {
                if (maskScalar[i] != maskAvx2[i]) {
                    std::fprintf(stderr,
                        "frustum_cull mismatch at %zu (n=%zu): "
                        "scalar=%u avx2=%u  center=(%g,%g,%g) r=%g\n",
                        i, n,
                        unsigned(maskScalar[i]), unsigned(maskAvx2[i]),
                        centers[i].x, centers[i].y, centers[i].z,
                        radii[i]);
                    CHECK(false);
                }
            }
        }
        std::printf("[simd_avx2_equivalence_extended] frustum_cull OK\n");
    }

    // ---- 3. apply_transforms equivalence -------------------------------
    {
        using threadmaxx::Transform;
        const std::vector<std::size_t> sizes = {
            0, 1, 7, 8, 9, 16, 17, 31, 64, 257, 1024
        };
        std::uniform_real_distribution<float> posDist(-5.0f, 5.0f);
        std::uniform_real_distribution<float> quatDist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> scaleDist(0.5f, 2.0f);
        std::uniform_real_distribution<float> ptDist(-3.0f, 3.0f);
        for (std::size_t n : sizes) {
            std::vector<Transform> ts(n);
            std::vector<Vec3>      pts(n);
            for (std::size_t i = 0; i < n; ++i) {
                ts[i].position = Vec3{posDist(rng), posDist(rng), posDist(rng)};
                // Build a unit quat from random components.
                Quat q{quatDist(rng), quatDist(rng), quatDist(rng), quatDist(rng)};
                const float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
                if (len > 0.0f) {
                    q = Quat{q.x/len, q.y/len, q.z/len, q.w/len};
                } else {
                    q = Quat{0, 0, 0, 1};
                }
                ts[i].orientation = q;
                ts[i].scale = Vec3{scaleDist(rng), scaleDist(rng), scaleDist(rng)};
                pts[i] = Vec3{ptDist(rng), ptDist(rng), ptDist(rng)};
            }
            std::vector<Vec3> outScalar(n), outAvx2(n);
            scalar::apply_transforms(ts, pts, outScalar);
            avx2::apply_transforms  (ts, pts, outAvx2);
            for (std::size_t i = 0; i < n; ++i) {
                if (!approxEq(outScalar[i], outAvx2[i])) {
                    std::fprintf(stderr,
                        "apply_transforms mismatch at %zu (n=%zu): "
                        "scalar=(%g,%g,%g) avx2=(%g,%g,%g)\n",
                        i, n,
                        outScalar[i].x, outScalar[i].y, outScalar[i].z,
                        outAvx2[i].x,   outAvx2[i].y,   outAvx2[i].z);
                    CHECK(false);
                }
            }
        }
        std::printf("[simd_avx2_equivalence_extended] apply_transforms OK\n");
    }

    // ---- 4. integrate_linear_motion equivalence ------------------------
    {
        using threadmaxx::Transform;
        const std::vector<std::size_t> sizes = {
            0, 1, 7, 8, 9, 16, 31, 64, 257, 1024
        };
        std::uniform_real_distribution<float> dist2(-5.0f, 5.0f);
        const float dt = 0.0125f;
        for (std::size_t n : sizes) {
            std::vector<Transform> ts(n);
            std::vector<Vec3>      vels(n);
            for (std::size_t i = 0; i < n; ++i) {
                ts[i].position = Vec3{dist2(rng), dist2(rng), dist2(rng)};
                ts[i].orientation = Quat{0, 0, 0, 1};
                ts[i].scale = Vec3{1, 1, 1};
                vels[i] = Vec3{dist2(rng), dist2(rng), dist2(rng)};
            }
            std::vector<Transform> tsScalar = ts;
            std::vector<Transform> tsAvx2   = ts;
            scalar::integrate_linear_motion(tsScalar, vels, dt);
            avx2::integrate_linear_motion  (tsAvx2,   vels, dt);
            for (std::size_t i = 0; i < n; ++i) {
                if (!approxEq(tsScalar[i].position, tsAvx2[i].position)) {
                    std::fprintf(stderr,
                        "integrate_linear_motion mismatch at %zu (n=%zu): "
                        "scalar=(%g,%g,%g) avx2=(%g,%g,%g)\n",
                        i, n,
                        tsScalar[i].position.x, tsScalar[i].position.y, tsScalar[i].position.z,
                        tsAvx2[i].position.x,   tsAvx2[i].position.y,   tsAvx2[i].position.z);
                    CHECK(false);
                }
            }
        }
        std::printf("[simd_avx2_equivalence_extended] integrate_linear_motion OK\n");
    }

    // ---- 5. transform_aabb equivalence ---------------------------------
    {
        using threadmaxx::Transform;
        using threadmaxx::BoundingVolume;
        const std::vector<std::size_t> sizes = {
            0, 1, 4, 16, 64, 257, 1024
        };
        std::uniform_real_distribution<float> posDist(-3.0f, 3.0f);
        std::uniform_real_distribution<float> quatDist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> scaleDist(0.5f, 2.0f);
        std::uniform_real_distribution<float> extDist(0.1f, 2.0f);
        for (std::size_t n : sizes) {
            std::vector<Transform>      ts(n);
            std::vector<BoundingVolume> bvIn(n);
            for (std::size_t i = 0; i < n; ++i) {
                ts[i].position = Vec3{posDist(rng), posDist(rng), posDist(rng)};
                Quat q{quatDist(rng), quatDist(rng), quatDist(rng), quatDist(rng)};
                const float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
                ts[i].orientation = (len > 0.0f)
                    ? Quat{q.x/len, q.y/len, q.z/len, q.w/len}
                    : Quat{0, 0, 0, 1};
                ts[i].scale = Vec3{scaleDist(rng), scaleDist(rng), scaleDist(rng)};
                const Vec3 c{posDist(rng), posDist(rng), posDist(rng)};
                const Vec3 e{extDist(rng), extDist(rng), extDist(rng)};
                bvIn[i].min = Vec3{c.x - e.x, c.y - e.y, c.z - e.z};
                bvIn[i].max = Vec3{c.x + e.x, c.y + e.y, c.z + e.z};
            }
            std::vector<BoundingVolume> outScalar(n), outAvx2(n);
            scalar::transform_aabb(ts, bvIn, outScalar);
            avx2::transform_aabb  (ts, bvIn, outAvx2);
            for (std::size_t i = 0; i < n; ++i) {
                if (!approxEq(outScalar[i], outAvx2[i])) {
                    std::fprintf(stderr,
                        "transform_aabb mismatch at %zu (n=%zu): "
                        "scalar.min=(%g,%g,%g) avx2.min=(%g,%g,%g)\n",
                        i, n,
                        outScalar[i].min.x, outScalar[i].min.y, outScalar[i].min.z,
                        outAvx2[i].min.x,   outAvx2[i].min.y,   outAvx2[i].min.z);
                    CHECK(false);
                }
            }
        }
        std::printf("[simd_avx2_equivalence_extended] transform_aabb OK\n");
    }

    EXIT_WITH_RESULT();
#endif
}
