// threadmaxx_simd — scalar Vec3 kernel tests.
//
// Verifies the six S1 kernels: add, sub, scale, madd, normalize, dot.
//
// Coverage:
//   1. Fixed-input correctness for each kernel.
//   2. Tail handling at 1, 3, 7, 13 elements (odd counts that
//      eventually won't align to SIMD lane widths in S3).
//   3. Mismatched-size policy: writers stop at the shorter span.
//   4. Empty-span policy: writers are no-ops; `dot` returns 0.
//   5. Zero-vector `normalize` stays zero (no NaN).
//   6. The `span_view`-shaped overloads route to the same impls.

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx_simd/vec3_ops.hpp>
#include <threadmaxx_simd/views.hpp>

#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace {

bool approxEq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool approxEq(const threadmaxx::Vec3& a, const threadmaxx::Vec3& b, float eps = 1e-5f) {
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) && approxEq(a.z, b.z, eps);
}

} // namespace

int main() {
    using threadmaxx::Vec3;
    namespace simd = threadmaxx::simd;

    // ---- 1. add / sub / scale / madd correctness (3 elements) -----------
    {
        std::vector<Vec3> a = {{1, 2, 3}, {4, 5, 6}, {-1, -2, -3}};
        std::vector<Vec3> b = {{10, 20, 30}, {40, 50, 60}, {1, 1, 1}};
        std::vector<Vec3> out(3);

        simd::add(a, b, out);
        CHECK(approxEq(out[0], Vec3{11, 22, 33}));
        CHECK(approxEq(out[1], Vec3{44, 55, 66}));
        CHECK(approxEq(out[2], Vec3{0, -1, -2}));

        simd::sub(a, b, out);
        CHECK(approxEq(out[0], Vec3{-9, -18, -27}));
        CHECK(approxEq(out[2], Vec3{-2, -3, -4}));

        simd::scale(a, 2.5f, out);
        CHECK(approxEq(out[0], Vec3{2.5f, 5.0f, 7.5f}));
        CHECK(approxEq(out[1], Vec3{10.0f, 12.5f, 15.0f}));

        // out = a + b * s
        simd::madd(a, b, 0.5f, out);
        CHECK(approxEq(out[0], Vec3{1.0f + 5.0f, 2.0f + 10.0f, 3.0f + 15.0f}));
        CHECK(approxEq(out[1], Vec3{4.0f + 20.0f, 5.0f + 25.0f, 6.0f + 30.0f}));
        std::printf("[simd_vec3] add/sub/scale/madd correctness OK\n");
    }

    // ---- 2. normalize / dot ----------------------------------------------
    {
        std::vector<Vec3> in = {{3, 0, 0}, {0, 4, 0}, {1, 1, 1}};
        std::vector<Vec3> out(3);
        simd::normalize(in, out);
        CHECK(approxEq(out[0], Vec3{1, 0, 0}));
        CHECK(approxEq(out[1], Vec3{0, 1, 0}));
        const float inv_sqrt3 = 1.0f / std::sqrt(3.0f);
        CHECK(approxEq(out[2], Vec3{inv_sqrt3, inv_sqrt3, inv_sqrt3}));

        std::vector<Vec3> u = {{1, 2, 3}, {4, 5, 6}};
        std::vector<Vec3> v = {{7, 8, 9}, {10, 11, 12}};
        // dot = (1*7 + 2*8 + 3*9) + (4*10 + 5*11 + 6*12)
        //     = (7 + 16 + 27) + (40 + 55 + 72) = 50 + 167 = 217
        const float d = simd::dot(u, v);
        CHECK(approxEq(d, 217.0f));
        std::printf("[simd_vec3] normalize/dot correctness OK\n");
    }

    // ---- 3. tail-size handling (1, 3, 7, 13 elements) ------------------
    {
        for (std::size_t n : {std::size_t{1}, std::size_t{3},
                              std::size_t{7}, std::size_t{13}}) {
            std::vector<Vec3> a(n), b(n), out(n);
            for (std::size_t i = 0; i < n; ++i) {
                a[i] = Vec3{static_cast<float>(i + 1),
                            static_cast<float>(i + 2),
                            static_cast<float>(i + 3)};
                b[i] = Vec3{1, 1, 1};
            }
            simd::add(a, b, out);
            for (std::size_t i = 0; i < n; ++i) {
                CHECK(approxEq(out[i], Vec3{
                    static_cast<float>(i + 2),
                    static_cast<float>(i + 3),
                    static_cast<float>(i + 4)}));
            }
        }
        std::printf("[simd_vec3] tail sizes 1/3/7/13 OK\n");
    }

    // ---- 4. mismatched-size policy: writers stop at the shorter span ---
    {
        std::vector<Vec3> a(5, Vec3{1, 1, 1});
        std::vector<Vec3> b(3, Vec3{2, 2, 2});  // shorter
        std::vector<Vec3> out(5, Vec3{99, 99, 99});  // pre-fill with sentinel

        simd::add(a, b, out);
        // First 3 elements written; the last 2 keep the sentinel.
        CHECK(approxEq(out[0], Vec3{3, 3, 3}));
        CHECK(approxEq(out[1], Vec3{3, 3, 3}));
        CHECK(approxEq(out[2], Vec3{3, 3, 3}));
        CHECK(approxEq(out[3], Vec3{99, 99, 99}));
        CHECK(approxEq(out[4], Vec3{99, 99, 99}));

        // dot also stops at the shorter span.
        const float d = simd::dot(a, b);  // 3 * (1*2 + 1*2 + 1*2) = 18
        CHECK(approxEq(d, 18.0f));
        std::printf("[simd_vec3] mismatched-size policy OK\n");
    }

    // ---- 5. empty-span policy --------------------------------------------
    {
        std::vector<Vec3> empty;
        std::vector<Vec3> out;
        simd::add(empty, empty, out);  // no-op, no crash
        CHECK_EQ(simd::dot(empty, empty), 0.0f);
        std::printf("[simd_vec3] empty span OK\n");
    }

    // ---- 6. zero-vector normalize stays zero -----------------------------
    {
        std::vector<Vec3> in = {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}};
        std::vector<Vec3> out(3);
        simd::normalize(in, out);
        CHECK(approxEq(out[0], Vec3{0, 0, 0}));
        CHECK(approxEq(out[1], Vec3{1, 0, 0}));
        CHECK(approxEq(out[2], Vec3{0, 0, 0}));
        // No NaN.
        CHECK(!std::isnan(out[0].x));
        CHECK(!std::isnan(out[2].z));
        std::printf("[simd_vec3] zero-vector normalize stays zero OK\n");
    }

    // ---- 7. span_view-shaped overloads route correctly ------------------
    {
        std::vector<Vec3> a = {{1, 2, 3}};
        std::vector<Vec3> b = {{4, 5, 6}};
        std::vector<Vec3> out(1);
        auto va  = simd::view(std::span<const Vec3>(a));
        auto vb  = simd::view(std::span<const Vec3>(b));
        auto vo  = simd::view(std::span<Vec3>(out));
        simd::add(va, vb, vo);
        CHECK(approxEq(out[0], Vec3{5, 7, 9}));
        std::printf("[simd_vec3] span_view overload OK\n");
    }

    EXIT_WITH_RESULT();
}
