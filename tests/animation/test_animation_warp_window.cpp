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
    const Vec3 authored{1.0f, 2.0f, 3.0f};

    WarpRequest r;
    r.from = Vec3{0.0f, 0.0f, 0.0f};
    r.to = Vec3{10.0f, 10.0f, 10.0f};
    r.startTime = 2.0f;
    r.endTime = 4.0f;
    r.weight = 1.0f;

    // 1. Before window — pose unmodified.
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 1.5f, r);
        CHECK(!res.applied);
        CHECK(nearly(joints[0].translation, authored));
    }

    // 2. After window — pose unmodified.
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 4.5f, r);
        CHECK(!res.applied);
        CHECK(nearly(joints[0].translation, authored));
    }

    // 3. Exactly at startTime — applied with alpha=0, contribution=0,
    //    pose effectively unmodified but result.applied is true.
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 2.0f, r);
        CHECK(res.applied);
        CHECK(nearly(res.alpha, 0.0f));
        CHECK(nearly(joints[0].translation, authored));
    }

    // 4. Exactly at endTime — applied with alpha=1, full offset.
    {
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 4.0f, r);
        CHECK(res.applied);
        CHECK(nearly(res.alpha, 1.0f));
        CHECK(nearly(joints[0].translation, Vec3{11.0f, 12.0f, 13.0f}));
    }

    // 5. Degenerate window (endTime < startTime) — no-op.
    {
        WarpRequest bad = r;
        bad.startTime = 5.0f;
        bad.endTime = 3.0f;
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 4.0f, bad);
        CHECK(!res.applied);
        CHECK(nearly(joints[0].translation, authored));
    }

    // 6. Out-of-range jointIndex — no-op.
    {
        WarpRequest bad = r;
        bad.jointIndex = 5;
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 3.0f, bad);
        CHECK(!res.applied);
        CHECK(nearly(joints[0].translation, authored));
    }

    // 7. Empty pose — no-op.
    {
        std::vector<JointPose> joints;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 3.0f, r);
        CHECK(!res.applied);
    }

    // 8. Instantaneous window (startTime == endTime == time) — alpha=1,
    //    full offset applied.
    {
        WarpRequest inst = r;
        inst.startTime = 3.0f;
        inst.endTime = 3.0f;
        std::vector<JointPose> joints(1);
        joints[0].translation = authored;
        PoseSpan pose{std::span<JointPose>(joints)};
        const WarpResult res = applyWarp(pose, 3.0f, inst);
        CHECK(res.applied);
        CHECK(nearly(res.alpha, 1.0f));
        CHECK(nearly(joints[0].translation, Vec3{11.0f, 12.0f, 13.0f}));
    }

    EXIT_WITH_RESULT();
}
