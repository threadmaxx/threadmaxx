#include "threadmaxx_animation/retarget.hpp"

#include <cstdint>

namespace threadmaxx::animation {

RetargetMap buildRetargetMap(const SkeletonDesc& src,
                             const SkeletonDesc& dst) {
    RetargetMap map;
    map.sourceName = src.name;
    map.destName = dst.name;
    map.mappings.reserve(src.joints.size());

    for (std::size_t s = 0; s < src.joints.size(); ++s) {
        const std::string& srcName = src.joints[s].name;
        if (srcName.empty()) continue;  // unnamed root or filler.
        for (std::size_t d = 0; d < dst.joints.size(); ++d) {
            if (dst.joints[d].name == srcName) {
                map.mappings.push_back(RetargetJointMapping{
                    static_cast<std::uint32_t>(s),
                    static_cast<std::uint32_t>(d),
                });
                break;
            }
        }
    }
    return map;
}

void retargetPose(std::span<const JointPose> srcPose,
                  std::span<JointPose> dstPose,
                  const RetargetMap& map,
                  RetargetChannels channels) noexcept {
    const std::size_t srcN = srcPose.size();
    const std::size_t dstN = dstPose.size();
    for (const auto& m : map.mappings) {
        if (m.srcIndex >= srcN) continue;
        if (m.dstIndex >= dstN) continue;
        const JointPose& s = srcPose[m.srcIndex];
        JointPose& d = dstPose[m.dstIndex];
        if (channels.copyRotation)    d.rotation    = s.rotation;
        if (channels.copyTranslation) d.translation = s.translation;
        if (channels.copyScale)       d.scale       = s.scale;
    }
}

} // namespace threadmaxx::animation
