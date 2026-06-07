#include "Check.hpp"
#include "threadmaxx_animation/warp.hpp"

#include <vector>

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-4f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-4f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

}  // namespace

int main() {
    // Authored root at (0,0,0), warp target (4,0,0). At endTime the
    // offset is (to-from)*weight; weights 0.0/0.5/1.0 should produce
    // 0%/50%/100% of the offset.
    const Vec3 authored{0.0f, 0.0f, 0.0f};

    WarpRequest r;
    r.from = Vec3{0.0f, 0.0f, 0.0f};
    r.to = Vec3{4.0f, 0.0f, 0.0f};
    r.startTime = 0.0f;
    r.endTime = 1.0f;

    // 1. weight = 1.0 at endTime → full warp, root = (4,0,0).
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        r.weight = 1.0f;
        const WarpResult res = applyWarp(pose, 1.0f, r);
        CHECK(res.applied);
        CHECK(nearly(joints[0].translation, Vec3{4.0f, 0.0f, 0.0f}));
    }

    // 2. weight = 0.5 at endTime → halfway between authored and target.
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        r.weight = 0.5f;
        const WarpResult res = applyWarp(pose, 1.0f, r);
        CHECK(res.applied);
        CHECK(nearly(joints[0].translation, Vec3{2.0f, 0.0f, 0.0f}));
    }

    // 3. weight = 0.0 at endTime → no warp contribution; pose unchanged.
    //    Still flagged as applied because we are inside the window.
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        r.weight = 0.0f;
        const WarpResult res = applyWarp(pose, 1.0f, r);
        CHECK(res.applied);
        CHECK(nearly(joints[0].translation, authored));
    }

    // 4. weight = 0.25 at mid-window (alpha=0.5):
    //    contribution = (4,0,0) * 0.5 * 0.25 = (0.5, 0, 0).
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        r.weight = 0.25f;
        const WarpResult res = applyWarp(pose, 0.5f, r);
        CHECK(res.applied);
        CHECK(nearly(res.alpha, 0.5f));
        CHECK(nearly(joints[0].translation, Vec3{0.5f, 0.0f, 0.0f}));
    }

    // 5. weight = 1.0 with non-zero `from`: offset is (to - from), not
    //    just `to`. authored at (1,1,1), from=(0.5, 0.5, 0.5),
    //    to=(2.5, 0.5, 0.5). At endTime: result = (1,1,1) + (2,0,0)
    //    = (3,1,1).
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = Vec3{1.0f, 1.0f, 1.0f};
        PoseSpan pose{std::span<JointPose>(joints)};
        WarpRequest r2;
        r2.from = Vec3{0.5f, 0.5f, 0.5f};
        r2.to = Vec3{2.5f, 0.5f, 0.5f};
        r2.startTime = 0.0f;
        r2.endTime = 1.0f;
        r2.weight = 1.0f;
        const WarpResult res = applyWarp(pose, 1.0f, r2);
        CHECK(res.applied);
        CHECK(nearly(joints[0].translation, Vec3{3.0f, 1.0f, 1.0f}));
    }

    EXIT_WITH_RESULT();
}
