#pragma once

#include "threadmaxx_animation/pose.hpp"

#include <string>
#include <vector>

namespace threadmaxx::animation {

/// One joint in a skeleton hierarchy. `parent == -1` marks the root.
/// `bindLocal` is the rest-pose transform expressed in the parent's
/// frame; the registry derives `bindGlobal` from these at registration
/// time (A2+). For A1 the bindGlobal slot in SkeletonDesc is filled
/// by the caller for round-trip testing.
///
/// JointPose is used for bindLocal rather than a 4x4 matrix —
/// chosen for symmetry with the rest of the pose data model and to
/// keep the lib free of a Mat4 dependency until a real consumer asks
/// for one.
struct Joint {
    std::string name;
    int parent = -1;
    JointPose bindLocal{};
};

struct SkeletonDesc {
    std::string name;
    std::vector<Joint> joints;
    std::vector<JointPose> bindGlobal;
};

} // namespace threadmaxx::animation
