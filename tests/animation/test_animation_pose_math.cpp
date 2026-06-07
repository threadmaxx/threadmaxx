#include "Check.hpp"
#include "threadmaxx_animation/detail/pose_math.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <array>
#include <cmath>

using namespace threadmaxx::animation;
using detail::compose;
using detail::lerp_pose;
using detail::blend_pose_weighted;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-5f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-5f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

constexpr bool nearly(const Quat& a, const Quat& b, float eps = 1e-5f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) &&
           nearly(a.z, b.z, eps) && nearly(a.w, b.w, eps);
}

Quat axisAngle(float ax, float ay, float az, float radians) {
    const float h = radians * 0.5f;
    const float s = std::sin(h);
    return Quat{ax * s, ay * s, az * s, std::cos(h)};
}

} // namespace

int main() {
    // 1. compose() matches manual reference: parent at +X, rotate 90°
    // about Y, scale 1; child local at +Z. Expected world position is
    // parent.position + rotate(parent.rot, parent.scale ⊙ local.pos).
    {
        JointPose parent;
        parent.translation = {1.0f, 0.0f, 0.0f};
        parent.rotation = axisAngle(0, 1, 0, 1.5707963267948966f); // 90° Y
        parent.scale = {1, 1, 1};

        JointPose local;
        local.translation = {0.0f, 0.0f, 1.0f};
        local.rotation = {0, 0, 0, 1};
        local.scale = {1, 1, 1};

        JointPose world = compose(parent, local);
        // Rotating +Z by 90° around Y → +X. Add parent +X → world = (2,0,0).
        CHECK(nearly(world.translation, Vec3{2.0f, 0.0f, 0.0f}, 1e-5f));
        CHECK(nearly(world.scale, Vec3{1, 1, 1}, 1e-6f));
    }

    // 2. compose() propagates scale componentwise.
    {
        JointPose parent;
        parent.scale = {2, 3, 4};
        parent.rotation = {0, 0, 0, 1};
        parent.translation = {0, 0, 0};
        JointPose local;
        local.scale = {1.5f, 0.5f, 2.0f};
        local.rotation = {0, 0, 0, 1};
        local.translation = {1, 1, 1};

        JointPose world = compose(parent, local);
        CHECK(nearly(world.scale, Vec3{3.0f, 1.5f, 8.0f}, 1e-6f));
        // Translation: parent.position(0) + rotate(identity, parent.scale ⊙ local.pos)
        //            = (2*1, 3*1, 4*1) = (2,3,4).
        CHECK(nearly(world.translation, Vec3{2.0f, 3.0f, 4.0f}, 1e-6f));
    }

    // 3. lerp at α=0 hits endpoint A, α=1 hits endpoint B.
    {
        JointPose a;
        a.translation = {0, 0, 0};
        a.rotation = {0, 0, 0, 1};
        a.scale = {1, 1, 1};

        JointPose b;
        b.translation = {10, 20, 30};
        b.rotation = axisAngle(1, 0, 0, 1.0472f); // 60° X
        b.scale = {2, 2, 2};

        PoseBuffer out(1);
        std::array<JointPose, 1> aArr{a};
        std::array<JointPose, 1> bArr{b};

        lerp_pose(aArr, bArr, 0.0f, out.localPose().joints);
        CHECK(nearly(out.localPose().joints[0].translation, a.translation));
        CHECK(nearly(out.localPose().joints[0].scale, a.scale));
        CHECK(nearly(out.localPose().joints[0].rotation, a.rotation, 1e-5f));

        lerp_pose(aArr, bArr, 1.0f, out.localPose().joints);
        CHECK(nearly(out.localPose().joints[0].translation, b.translation, 1e-5f));
        CHECK(nearly(out.localPose().joints[0].scale, b.scale, 1e-5f));
        CHECK(nearly(out.localPose().joints[0].rotation, b.rotation, 1e-5f));
    }

    // 4. 50/50 blend of two 4-joint poses matches scalar reference
    // within 1e-6 — straight midpoint per JointPose, with normalized
    // quaternions.
    {
        std::array<JointPose, 4> aArr{};
        std::array<JointPose, 4> bArr{};
        for (std::size_t i = 0; i < 4; ++i) {
            aArr[i].translation = Vec3{static_cast<float>(i), 0.0f, 0.0f};
            aArr[i].rotation = {0, 0, 0, 1};
            aArr[i].scale = {1, 1, 1};

            bArr[i].translation = Vec3{0.0f, static_cast<float>(i + 1), 0.0f};
            bArr[i].rotation = {0, 0, 0, 1};
            bArr[i].scale = {2, 2, 2};
        }

        PoseBuffer out(4);
        blend_pose_weighted(aArr, bArr, 0.5f, out.localPose().joints);

        for (std::size_t i = 0; i < 4; ++i) {
            const Vec3 expectT{
                0.5f * static_cast<float>(i),
                0.5f * static_cast<float>(i + 1),
                0.0f,
            };
            CHECK(nearly(out.localPose().joints[i].translation, expectT, 1e-6f));
            CHECK(nearly(out.localPose().joints[i].scale, Vec3{1.5f, 1.5f, 1.5f}, 1e-6f));
            // Both rotations were identity → result identity (up to
            // nlerp normalization, which is exact in this case).
            CHECK(nearly(out.localPose().joints[i].rotation, Quat{0, 0, 0, 1}, 1e-6f));
        }
    }

    EXIT_WITH_RESULT();
}
