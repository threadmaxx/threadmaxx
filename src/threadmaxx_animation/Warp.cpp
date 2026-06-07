#include "threadmaxx_animation/warp.hpp"

namespace threadmaxx::animation {

WarpResult applyWarp(PoseSpan pose, float time, const WarpRequest& request) noexcept {
    WarpResult result{};
    if (pose.joints.empty()) return result;
    if (static_cast<std::size_t>(request.jointIndex) >= pose.joints.size()) return result;
    if (request.endTime < request.startTime) return result;
    if (time < request.startTime || time > request.endTime) return result;

    const float span = request.endTime - request.startTime;
    const float alpha = (span > 0.0f) ? (time - request.startTime) / span : 1.0f;
    const float k = alpha * request.weight;

    JointPose& j = pose.joints[request.jointIndex];
    j.translation.x += (request.to.x - request.from.x) * k;
    j.translation.y += (request.to.y - request.from.y) * k;
    j.translation.z += (request.to.z - request.from.z) * k;

    result.applied = true;
    result.alpha = alpha;
    return result;
}

}  // namespace threadmaxx::animation
