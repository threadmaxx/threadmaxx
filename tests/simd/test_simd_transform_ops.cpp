// threadmaxx_simd — scalar Transform kernel tests.
//
// Verifies:
//   1. `apply_transforms` with identity → out == points.
//   2. `apply_transforms` with pure translation → out shifted by t.position.
//   3. `apply_transforms` with 90° rotation around Y → (1,0,0) → (0,0,-1).
//      (Right-hand rule, looking down the +Y axis: +X rotates toward -Z.)
//   4. `apply_transforms` with scale → out components scaled in-place.
//   5. `integrate_linear_motion` advances position by v * dt.
//   6. `integrate_positions` advances both position and orientation.
//   7. Zero angular velocity leaves orientation untouched.
//   8. Tail / mismatched / empty cases.

#include "Check.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx_simd/transform_ops.hpp>

#include <cmath>
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

bool approxEq(const threadmaxx::Quat& a, const threadmaxx::Quat& b,
              float eps = 1e-5f) {
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) &&
           approxEq(a.z, b.z, eps) && approxEq(a.w, b.w, eps);
}

threadmaxx::Quat quatFromAxisAngle(float ax, float ay, float az, float angle) {
    const float h = 0.5f * angle;
    const float s = std::sin(h);
    return threadmaxx::Quat{ax * s, ay * s, az * s, std::cos(h)};
}

} // namespace

int main() {
    using threadmaxx::Vec3;
    using threadmaxx::Quat;
    using threadmaxx::Transform;
    using threadmaxx::Velocity;
    namespace simd = threadmaxx::simd;

    // ---- 1. apply_transforms identity -----------------------------------
    {
        std::vector<Transform> t = {Transform{}};   // pos=0, orient=identity, scale=(1,1,1)
        std::vector<Vec3> points = {{1, 2, 3}};
        std::vector<Vec3> out(1);
        simd::apply_transforms(t, points, out);
        CHECK(approxEq(out[0], Vec3{1, 2, 3}));
        std::printf("[simd_transform] identity OK\n");
    }

    // ---- 2. apply_transforms pure translation ---------------------------
    {
        Transform tr{};
        tr.position = Vec3{10, 20, 30};
        std::vector<Transform> t = {tr};
        std::vector<Vec3> points = {{1, 2, 3}};
        std::vector<Vec3> out(1);
        simd::apply_transforms(t, points, out);
        CHECK(approxEq(out[0], Vec3{11, 22, 33}));
        std::printf("[simd_transform] translation OK\n");
    }

    // ---- 3. apply_transforms 90° rotation around Y ----------------------
    {
        Transform tr{};
        tr.orientation = quatFromAxisAngle(0, 1, 0, 3.14159265f / 2.0f);
        std::vector<Transform> t = {tr};
        // Point (1, 0, 0). Right-hand rule, rotating around +Y by 90°:
        // +X axis → -Z. So output should be (0, 0, -1).
        std::vector<Vec3> points = {{1, 0, 0}};
        std::vector<Vec3> out(1);
        simd::apply_transforms(t, points, out);
        CHECK(approxEq(out[0], Vec3{0, 0, -1}, 1e-5f));
        std::printf("[simd_transform] rotation OK\n");
    }

    // ---- 4. apply_transforms scale --------------------------------------
    {
        Transform tr{};
        tr.scale = Vec3{2, 3, 4};
        std::vector<Transform> t = {tr};
        std::vector<Vec3> points = {{1, 1, 1}};
        std::vector<Vec3> out(1);
        simd::apply_transforms(t, points, out);
        CHECK(approxEq(out[0], Vec3{2, 3, 4}));
        std::printf("[simd_transform] scale OK\n");
    }

    // ---- 5. integrate_linear_motion -------------------------------------
    {
        std::vector<Transform> t(2);
        t[0].position = Vec3{0, 0, 0};
        t[1].position = Vec3{10, 0, 0};
        std::vector<Vec3> velocity = {{1, 2, 3}, {0, -1, 0}};
        simd::integrate_linear_motion(t, velocity, 0.5f);
        CHECK(approxEq(t[0].position, Vec3{0.5f, 1.0f, 1.5f}));
        CHECK(approxEq(t[1].position, Vec3{10.0f, -0.5f, 0.0f}));
        // Orientation untouched.
        CHECK(approxEq(t[0].orientation, Quat{0, 0, 0, 1}));
        std::printf("[simd_transform] integrate_linear_motion OK\n");
    }

    // ---- 6. integrate_positions: linear + angular -----------------------
    {
        std::vector<Transform> t(1);
        t[0].position = Vec3{0, 0, 0};
        t[0].orientation = Quat{0, 0, 0, 1};   // identity
        std::vector<Velocity> v(1);
        v[0].linear = Vec3{2, 0, 0};
        v[0].angular = Vec3{0, 3.14159265f, 0};  // π rad/s around Y
        simd::integrate_positions(t, v, 1.0f);  // 1 second
        CHECK(approxEq(t[0].position, Vec3{2, 0, 0}));
        // After 1s at π rad/s around Y → 180° rotation. Apply that to
        // (1,0,0) should give (-1, 0, 0).
        const Vec3 testPt{1, 0, 0};
        std::vector<Transform> t2 = {t[0]};
        std::vector<Vec3> pts = {testPt};
        std::vector<Vec3> out(1);
        simd::apply_transforms(t2, pts, out);
        // out is t2[0].position + rotate(orientation, scale*testPt)
        //     = (2,0,0) + rotate(180°-around-Y, (1,0,0))
        //     = (2,0,0) + (-1, 0, 0)
        //     = (1, 0, 0)
        CHECK(approxEq(out[0], Vec3{1, 0, 0}, 1e-4f));
        std::printf("[simd_transform] integrate_positions OK\n");
    }

    // ---- 7. zero angular velocity → orientation untouched ---------------
    {
        std::vector<Transform> t(1);
        t[0].orientation = quatFromAxisAngle(1, 0, 0, 0.3f);
        const Quat before = t[0].orientation;
        std::vector<Velocity> v(1);
        v[0].linear = Vec3{1, 1, 1};
        v[0].angular = Vec3{0, 0, 0};
        simd::integrate_positions(t, v, 0.5f);
        CHECK(approxEq(t[0].orientation, before, 1e-6f));
        std::printf("[simd_transform] zero-angular preserves orientation OK\n");
    }

    // ---- 8. tail handling + mismatched + empty --------------------------
    {
        for (std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{13}}) {
            std::vector<Transform> t(n);
            std::vector<Vec3> v(n, Vec3{1, 1, 1});
            simd::integrate_linear_motion(t, v, 1.0f);
            for (const auto& trf : t) {
                CHECK(approxEq(trf.position, Vec3{1, 1, 1}));
            }
        }

        // Mismatched: writer stops at shorter span.
        std::vector<Transform> t(5);
        std::vector<Vec3> v(3, Vec3{1, 0, 0});
        simd::integrate_linear_motion(t, v, 1.0f);
        CHECK(approxEq(t[0].position, Vec3{1, 0, 0}));
        CHECK(approxEq(t[2].position, Vec3{1, 0, 0}));
        CHECK(approxEq(t[3].position, Vec3{0, 0, 0}));  // untouched

        // Empty.
        std::vector<Transform> empty_t;
        std::vector<Vec3> empty_v;
        simd::integrate_linear_motion(empty_t, empty_v, 0.5f);  // no-op
        std::printf("[simd_transform] tail/mismatched/empty OK\n");
    }

    EXIT_WITH_RESULT();
}
