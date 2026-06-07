#pragma once

#include <cstdint>

/// Public identifier aliases for the threadmaxx_animation library.
///
/// All ids are opaque handles into the AnimationRegistry. They are
/// trivially-copyable PODs; comparing ids is comparing the raw bit
/// pattern. Generation-tagged refs (see SkeletonRef) guard against
/// stale ids surviving a remove + re-add.
namespace threadmaxx::animation {

using SkeletonId = std::uint64_t;
using ClipId     = std::uint64_t;
using GraphId    = std::uint64_t;
using PoseId     = std::uint64_t;

/// Joint index inside a SkeletonDesc::joints array. Root is always 0
/// by convention (parents must have a lower index than their children
/// — validated at SkeletonDesc registration time in A2+).
struct JointId {
    std::uint32_t value{};

    constexpr bool operator==(const JointId&) const noexcept = default;
};

/// Generation-tagged reference to a registered skeleton. Becomes
/// invalid (isValid() == false on the owning registry) the moment
/// the underlying SkeletonDesc is removed.
struct SkeletonRef {
    SkeletonId id{};
    std::uint32_t generation{};

    constexpr bool operator==(const SkeletonRef&) const noexcept = default;
};

} // namespace threadmaxx::animation
