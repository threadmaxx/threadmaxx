// threadmaxx_simd — scalar Quat kernel tests.
//
// Verifies:
//   1. `normalize(span<Quat>)` is in-place and produces unit-length
//      quaternions; zero-norm input becomes identity (0,0,0,1).
//   2. `slerp` endpoints are exact at alpha=0 and alpha=1.
//   3. `slerp` midpoint matches a reference scalar slerp for both
//      short-path and shortest-path cases.
//   4. Tail handling: 1, 7, 13 elements.
//   5. Mismatched-size and empty-span policies.

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx_simd/quat_ops.hpp>

#include <cmath>
#include <cstdio>
#include <span>
#include <vector>

namespace {

bool approxEq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

bool approxEq(const threadmaxx::Quat& a, const threadmaxx::Quat& b,
              float eps = 1e-5f) {
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) &&
           approxEq(a.z, b.z, eps) && approxEq(a.w, b.w, eps);
}

float quatNorm(const threadmaxx::Quat& q) {
    return std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
}

} // namespace

int main() {
    using threadmaxx::Quat;
    namespace simd = threadmaxx::simd;

    // ---- 1. normalize basics --------------------------------------------
    {
        std::vector<Quat> q = {
            {2, 0, 0, 0},         // pure-imaginary, length 2
            {0, 0, 0, 5},         // identity-direction, length 5
            {1, 1, 1, 1},         // length 2
            {0, 0, 0, 0},         // zero — should fall back to identity
        };
        simd::normalize(std::span<Quat>(q));
        CHECK(approxEq(q[0], Quat{1, 0, 0, 0}));
        CHECK(approxEq(q[1], Quat{0, 0, 0, 1}));
        CHECK(approxEq(quatNorm(q[2]), 1.0f));
        CHECK(approxEq(q[3], Quat{0, 0, 0, 1}));  // identity fallback
        std::printf("[simd_quat] normalize OK\n");
    }

    // ---- 2. slerp endpoints exact --------------------------------------
    {
        const Quat a{0, 0, 0, 1};                              // identity
        const Quat b{0, std::sin(0.5f), 0, std::cos(0.5f)};    // ~57° around Y
        std::vector<Quat> av{a};
        std::vector<Quat> bv{b};
        std::vector<Quat> out(1);

        simd::slerp(av, bv, out, 0.0f);
        CHECK(approxEq(out[0], a));
        simd::slerp(av, bv, out, 1.0f);
        CHECK(approxEq(out[0], b));
        std::printf("[simd_quat] slerp endpoints OK\n");
    }

    // ---- 3. slerp midpoint matches axis-angle interpolation -------------
    {
        // Rotate 90° around Y, interpolate halfway → 45° around Y.
        const float a90 = 0.5f * (3.14159265f / 2.0f);
        const Quat a{0, 0, 0, 1};
        const Quat b{0, std::sin(a90), 0, std::cos(a90)};
        std::vector<Quat> av{a};
        std::vector<Quat> bv{b};
        std::vector<Quat> out(1);
        simd::slerp(av, bv, out, 0.5f);
        // Expected: 45° around Y → q = (0, sin(22.5°), 0, cos(22.5°))
        const float a225 = 0.5f * (3.14159265f / 4.0f);
        const Quat expected{0, std::sin(a225), 0, std::cos(a225)};
        CHECK(approxEq(out[0], expected, 1e-4f));
        CHECK(approxEq(quatNorm(out[0]), 1.0f, 1e-5f));
        std::printf("[simd_quat] slerp midpoint OK\n");
    }

    // ---- 4. shortest-path branch ---------------------------------------
    {
        // a = identity, b = -identity. dot(a, b) = -1. The shortest-path
        // branch should flip b → back to identity → slerp returns the
        // identity at alpha=0.5, not the zero quaternion.
        std::vector<Quat> av{Quat{0, 0, 0, 1}};
        std::vector<Quat> bv{Quat{0, 0, 0, -1}};
        std::vector<Quat> out(1);
        simd::slerp(av, bv, out, 0.5f);
        // After negating b, slerp(identity, identity, 0.5) = identity.
        CHECK(approxEq(out[0], Quat{0, 0, 0, 1}, 1e-5f));
        CHECK(approxEq(quatNorm(out[0]), 1.0f));
        std::printf("[simd_quat] slerp shortest-path OK\n");
    }

    // ---- 5. tail handling (1, 7, 13) ------------------------------------
    {
        for (std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{13}}) {
            std::vector<Quat> q(n);
            for (std::size_t i = 0; i < n; ++i) {
                q[i] = Quat{static_cast<float>(i + 1),
                            static_cast<float>(i + 2),
                            0.0f, 1.0f};
            }
            simd::normalize(std::span<Quat>(q));
            for (std::size_t i = 0; i < n; ++i) {
                CHECK(approxEq(quatNorm(q[i]), 1.0f, 1e-5f));
            }
        }
        std::printf("[simd_quat] tail 1/7/13 OK\n");
    }

    // ---- 6. mismatched sizes + empty spans ------------------------------
    {
        std::vector<Quat> a(5, Quat{0, 0, 0, 1});
        std::vector<Quat> b(3, Quat{0, 1, 0, 0});
        std::vector<Quat> out(5, Quat{99, 99, 99, 99});  // sentinels
        simd::slerp(a, b, out, 0.5f);
        // First 3 elements written; last 2 keep sentinel.
        CHECK(approxEq(quatNorm(out[0]), 1.0f, 1e-5f));
        CHECK(approxEq(out[3], Quat{99, 99, 99, 99}));
        CHECK(approxEq(out[4], Quat{99, 99, 99, 99}));

        std::vector<Quat> empty;
        simd::normalize(std::span<Quat>(empty));  // no-op
        simd::slerp(empty, empty, empty, 0.3f);   // no-op
        std::printf("[simd_quat] mismatched + empty OK\n");
    }

    EXIT_WITH_RESULT();
}
