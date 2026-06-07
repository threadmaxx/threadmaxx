#include "Check.hpp"
#include "threadmaxx_animation/diagnostics.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cmath>
#include <vector>

using namespace threadmaxx::animation;

namespace {

JointPose identityJoint() {
    return JointPose{{0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}};
}

} // namespace

int main() {
    // === 1. Empty pose validates clean.
    {
        std::vector<JointPose> empty;
        PoseValidationReport rep = validatePose(empty);
        CHECK(rep.ok());
        CHECK(!any(rep.overall));
        CHECK_EQ(rep.badJointCount, std::size_t{0});
    }

    // === 2. All-identity pose validates clean.
    {
        std::vector<JointPose> poses(4, identityJoint());
        PoseValidationReport rep = validatePose(poses);
        CHECK(rep.ok());
        CHECK_EQ(rep.badJointCount, std::size_t{0});
    }

    // === 3. NaN translation flags `NanTranslation`.
    {
        std::vector<JointPose> poses(3, identityJoint());
        poses[1].translation.x = std::nanf("");
        PoseValidationReport rep = validatePose(poses);
        CHECK(!rep.ok());
        CHECK(has(rep.overall, PoseIssue::NanTranslation));
        CHECK_EQ(rep.firstBadJoint, std::size_t{1});
        CHECK_EQ(rep.badJointCount, std::size_t{1});
    }

    // === 4. NaN rotation flags `NanRotation`.
    {
        std::vector<JointPose> poses(2, identityJoint());
        poses[0].rotation.w = std::nanf("");
        PoseValidationReport rep = validatePose(poses);
        CHECK(!rep.ok());
        CHECK(has(rep.overall, PoseIssue::NanRotation));
        CHECK_EQ(rep.firstBadJoint, std::size_t{0});
    }

    // === 5. Denormal rotation (`|q| ≈ 0`) flags `DenormalRotation`.
    {
        std::vector<JointPose> poses(2, identityJoint());
        poses[1].rotation = Quat{0, 0, 0, 0};
        PoseValidationReport rep = validatePose(poses);
        CHECK(has(rep.overall, PoseIssue::DenormalRotation));
        CHECK_EQ(rep.firstBadJoint, std::size_t{1});
    }

    // === 6. Negative-axis scale flags `DegenerateScale`. (Sign-flip
    // case — non-uniform but signed-positive is OK.)
    {
        std::vector<JointPose> poses(3, identityJoint());
        poses[2].scale.x = -1.0f;
        PoseValidationReport rep = validatePose(poses);
        CHECK(has(rep.overall, PoseIssue::DegenerateScale));
        CHECK_EQ(rep.firstBadJoint, std::size_t{2});
    }

    // === 7. Zero-axis scale also flags `DegenerateScale`.
    {
        std::vector<JointPose> poses(2, identityJoint());
        poses[0].scale.y = 0.0f;
        PoseValidationReport rep = validatePose(poses);
        CHECK(has(rep.overall, PoseIssue::DegenerateScale));
    }

    // === 8. Multiple distinct failures accumulate flags in `overall`.
    {
        std::vector<JointPose> poses(3, identityJoint());
        poses[0].translation.z = std::nanf("");
        poses[2].scale.x = -1.0f;
        PoseValidationReport rep = validatePose(poses);
        CHECK(has(rep.overall, PoseIssue::NanTranslation));
        CHECK(has(rep.overall, PoseIssue::DegenerateScale));
        CHECK_EQ(rep.badJointCount, std::size_t{2});
        CHECK_EQ(rep.firstBadJoint, std::size_t{0});  // smallest bad idx.
    }

    // === 9. Multiple flags on a single joint stack in the bit-set.
    {
        std::vector<JointPose> poses(1, identityJoint());
        poses[0].translation.x = std::nanf("");
        poses[0].rotation = Quat{0, 0, 0, 0};
        poses[0].scale.z = -1.0f;
        PoseValidationReport rep = validatePose(poses);
        CHECK(has(rep.overall, PoseIssue::NanTranslation));
        CHECK(has(rep.overall, PoseIssue::DenormalRotation));
        CHECK(has(rep.overall, PoseIssue::DegenerateScale));
        CHECK_EQ(rep.badJointCount, std::size_t{1});
    }

    // === 10. `validateJoint` per-joint entry — same classification as
    // the vector walk.
    {
        JointPose jp = identityJoint();
        CHECK(!any(validateJoint(jp)));
        jp.rotation.w = std::nanf("");
        CHECK(has(validateJoint(jp), PoseIssue::NanRotation));
    }

    // === 11. PoseSpan overload — uses the same code path.
    {
        std::vector<JointPose> poses(2, identityJoint());
        poses[1].translation.x = std::nanf("");
        PoseBuffer buf;
        buf.resize(2);
        for (std::size_t i = 0; i < 2; ++i) {
            buf.localPose().joints[i] = poses[i];
        }
        PoseValidationReport rep = validatePose(buf.localPose());
        CHECK(has(rep.overall, PoseIssue::NanTranslation));
    }

    EXIT_WITH_RESULT();
}
