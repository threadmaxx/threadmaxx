// threadmaxx_simd — scalar ↔ AVX2 equivalence for Vec3 kernels.
//
// Verifies that the AVX2 paths produce results indistinguishable
// (within float tolerance) from the scalar reference. RNG-driven
// inputs over a sweep of array sizes that exercise:
//   - Sub-lane sizes (n=1, 2): no AVX2 lanes used; tail-only paths.
//   - Exact-lane sizes (n=8, 16, 24): floats * 3 % 8 still has a tail.
//   - Cross-lane mismatch sizes (1024 + small offsets).
//
// Covered kernels:
//   - add / sub / scale / madd / dot — S3 (flat-float vectorized).
//   - normalize — S3.5 (gather-based AoS→SoA + sqrt+div + zero mask).
//
// When the build doesn't have AVX2 (`#if !THREADMAXX_SIMD_HAS_AVX2`),
// the test prints a skip line and exits 0. CTest still records a
// pass — the test is the "AVX2 path stays correct" gate; absence of
// AVX2 isn't a failure.

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx_simd/config.hpp>
#include <threadmaxx_simd/detail/scalar.hpp>

#if THREADMAXX_SIMD_HAS_AVX2
#  include <threadmaxx_simd/detail/avx2.hpp>
#endif

#include <cmath>
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

bool approxEq(const threadmaxx::Vec3& a, const threadmaxx::Vec3& b,
              float absEps = 1e-5f, float relEps = 1e-6f) {
    return approxEq(a.x, b.x, absEps, relEps) &&
           approxEq(a.y, b.y, absEps, relEps) &&
           approxEq(a.z, b.z, absEps, relEps);
}

bool allApproxEq(const std::vector<threadmaxx::Vec3>& a,
                 const std::vector<threadmaxx::Vec3>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!approxEq(a[i], b[i])) {
            std::fprintf(stderr,
                "mismatch at %zu: scalar=(%g,%g,%g) avx2=(%g,%g,%g)\n",
                i, a[i].x, a[i].y, a[i].z, b[i].x, b[i].y, b[i].z);
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
#if !THREADMAXX_SIMD_HAS_AVX2
    std::printf("[simd_vec3_avx2_equivalence] AVX2 not built; "
                "test skipped (this is OK for portable builds)\n");
    return 0;
#else
    using threadmaxx::Vec3;
    namespace scalar = threadmaxx::simd::detail::scalar;
    namespace avx2   = threadmaxx::simd::detail::avx2;

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);

    // Sweep sizes that span every interesting alignment boundary at
    // the float-array level (we step in 8-float chunks; n Vec3s gives
    // 3n floats — so the tail size is `(3n) % 8`).
    const std::vector<std::size_t> sizes = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 16, 17, 23, 24, 31, 32, 64, 127, 256, 1024
    };

    for (std::size_t n : sizes) {
        std::vector<Vec3> a(n), b(n);
        for (std::size_t i = 0; i < n; ++i) {
            a[i] = Vec3{dist(rng), dist(rng), dist(rng)};
            b[i] = Vec3{dist(rng), dist(rng), dist(rng)};
        }

        std::vector<Vec3> outScalar(n), outAvx2(n);

        // ---- add -------------------------------------------------------
        scalar::add(a, b, outScalar);
        avx2::add  (a, b, outAvx2);
        CHECK(allApproxEq(outScalar, outAvx2));

        // ---- sub -------------------------------------------------------
        scalar::sub(a, b, outScalar);
        avx2::sub  (a, b, outAvx2);
        CHECK(allApproxEq(outScalar, outAvx2));

        // ---- scale -----------------------------------------------------
        const float s = dist(rng);
        scalar::scale(a, s, outScalar);
        avx2::scale  (a, s, outAvx2);
        CHECK(allApproxEq(outScalar, outAvx2));

        // ---- madd ------------------------------------------------------
        scalar::madd(a, b, s, outScalar);
        avx2::madd  (a, b, s, outAvx2);
        // FMA introduces tiny last-bit differences vs `a + b*s`;
        // bump the relative tolerance for this one.
        bool maddOk = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (!approxEq(outScalar[i], outAvx2[i], 1e-4f, 1e-5f)) {
                std::fprintf(stderr,
                    "madd mismatch at %zu: scalar=(%g,%g,%g) avx2=(%g,%g,%g)\n",
                    i,
                    outScalar[i].x, outScalar[i].y, outScalar[i].z,
                    outAvx2[i].x,   outAvx2[i].y,   outAvx2[i].z);
                maddOk = false;
            }
        }
        CHECK(maddOk);

        // ---- dot -------------------------------------------------------
        const float dScalar = scalar::dot(a, b);
        const float dAvx2   = avx2::dot  (a, b);
        // dot accumulates 3*n products; absolute error grows with n.
        // Use a relative tolerance proportional to the scale.
        const float scaleHint = std::max(1.0f, std::fabs(dScalar));
        CHECK(std::fabs(dScalar - dAvx2) <= 1e-3f * scaleHint);

        // ---- normalize (S3.5) ------------------------------------------
        // AVX2 path uses sqrt+div (full-precision) so the result is
        // within ulp of the scalar reference. 1e-6 relative tolerance
        // per the FUTURE_WORK plan.
        scalar::normalize(a, outScalar);
        avx2::normalize  (a, outAvx2);
        bool normOk = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (!approxEq(outScalar[i], outAvx2[i], 1e-6f, 1e-6f)) {
                std::fprintf(stderr,
                    "normalize mismatch at %zu (n=%zu): scalar=(%g,%g,%g) avx2=(%g,%g,%g)\n",
                    i, n,
                    outScalar[i].x, outScalar[i].y, outScalar[i].z,
                    outAvx2[i].x,   outAvx2[i].y,   outAvx2[i].z);
                normOk = false;
            }
        }
        CHECK(normOk);
    }

    // ---- normalize: explicit zero-vector check ----------------------------
    // Verify the zero-mask path: zero-magnitude inputs must produce
    // zero output, not NaN. Mix zero vectors among non-zero ones at
    // various positions (within the 8-Vec3 lane AND in the tail).
    {
        std::vector<Vec3> zeroMix = {
            {1, 0, 0},          // index 0: non-zero
            {0, 0, 0},          // index 1: zero
            {0, 1, 0},          // index 2: non-zero
            {0, 0, 0},          // index 3: zero
            {0, 0, 1},          // index 4
            {3, 4, 0},          // index 5
            {0, 0, 0},          // index 6: zero in middle of lane
            {1, 1, 1},          // index 7
            // Tail (n=11 total)
            {0, 0, 0},          // index 8: zero in tail
            {2, 0, 0},          // index 9
            {0, 0, 0},          // index 10
        };
        std::vector<Vec3> outScalar(zeroMix.size());
        std::vector<Vec3> outAvx2(zeroMix.size());
        scalar::normalize(zeroMix, outScalar);
        avx2::normalize  (zeroMix, outAvx2);
        for (std::size_t i = 0; i < zeroMix.size(); ++i) {
            CHECK(!std::isnan(outAvx2[i].x));
            CHECK(!std::isnan(outAvx2[i].y));
            CHECK(!std::isnan(outAvx2[i].z));
            CHECK(approxEq(outScalar[i], outAvx2[i], 1e-6f, 1e-6f));
        }
    }

    std::printf("[simd_vec3_avx2_equivalence] AVX2 == scalar across "
                "%zu size points\n", sizes.size());
    EXIT_WITH_RESULT();
#endif
}
