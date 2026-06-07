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
    // Authored root-motion clip ends at endTime=1.0 with root at
    // (5,0,0). We want the warped result to end at (5,0,3) — sideways
    // by 3 in z. WarpRequest::from = authored end pos, ::to = target.
    // Sample at time = endTime, apply warp, expect root at (5,0,3).
    std::vector<JointPose> joints(3);
    joints[0].translation = Vec3{5.0f, 0.0f, 0.0f};
    joints[1].translation = Vec3{0.5f, 1.0f, 0.0f};
    joints[2].translation = Vec3{0.0f, 0.5f, 0.5f};
    const Vec3 j1Before = joints[1].translation;
    const Vec3 j2Before = joints[2].translation;
    PoseSpan pose{std::span<JointPose>(joints)};

    WarpRequest r;
    r.from = Vec3{5.0f, 0.0f, 0.0f};
    r.to = Vec3{5.0f, 0.0f, 3.0f};
    r.startTime = 0.0f;
    r.endTime = 1.0f;
    r.weight = 1.0f;
    r.jointIndex = 0;

    const WarpResult res = applyWarp(pose, 1.0f, r);

    CHECK(res.applied);
    CHECK(nearly(res.alpha, 1.0f));
    CHECK(nearly(joints[0].translation, Vec3{5.0f, 0.0f, 3.0f}));
    // Other joints must be untouched — warp targets jointIndex only.
    CHECK(nearly(joints[1].translation, j1Before));
    CHECK(nearly(joints[2].translation, j2Before));

    // Halfway through the window: alpha = 0.5, root authored to
    // (2.5,0,0), offset = (0,0,3) * 0.5 = (0,0,1.5), result (2.5,0,1.5).
    std::vector<JointPose> midPose(1);
    midPose[0].translation = Vec3{2.5f, 0.0f, 0.0f};
    PoseSpan p2{std::span<JointPose>(midPose)};
    const WarpResult res2 = applyWarp(p2, 0.5f, r);
    CHECK(res2.applied);
    CHECK(nearly(res2.alpha, 0.5f));
    CHECK(nearly(midPose[0].translation, Vec3{2.5f, 0.0f, 1.5f}));

    // Warping a non-root joint: jointIndex=2.
    std::vector<JointPose> handPose(3);
    handPose[0].translation = Vec3{0.0f, 0.0f, 0.0f};
    handPose[1].translation = Vec3{0.0f, 1.0f, 0.0f};
    handPose[2].translation = Vec3{1.0f, 1.0f, 0.0f};  // authored hand at endTime
    PoseSpan p3{std::span<JointPose>(handPose)};
    WarpRequest hr;
    hr.from = Vec3{1.0f, 1.0f, 0.0f};
    hr.to = Vec3{2.0f, 0.5f, 0.0f};
    hr.startTime = 0.0f;
    hr.endTime = 1.0f;
    hr.weight = 1.0f;
    hr.jointIndex = 2;
    const WarpResult res3 = applyWarp(p3, 1.0f, hr);
    CHECK(res3.applied);
    CHECK(nearly(handPose[0].translation, Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(nearly(handPose[1].translation, Vec3{0.0f, 1.0f, 0.0f}));
    CHECK(nearly(handPose[2].translation, Vec3{2.0f, 0.5f, 0.0f}));

    EXIT_WITH_RESULT();
}
