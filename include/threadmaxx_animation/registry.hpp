#pragma once

#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/skeleton.hpp"
#include "threadmaxx_animation/types.hpp"

#include <cstdint>
#include <vector>

/// Owner of registered skeletons and clips. Single-threaded by
/// contract — the engine integration pattern is a sim-thread preStep
/// that adds, followed by parallel worker reads of the immutable
/// SkeletonDesc / ClipDesc pointers returned from `getSkeleton` /
/// `getClip`. Generation tags catch use-after-remove.
namespace threadmaxx::animation {

class AnimationRegistry {
public:
    AnimationRegistry();
    ~AnimationRegistry();
    AnimationRegistry(const AnimationRegistry&) = delete;
    AnimationRegistry& operator=(const AnimationRegistry&) = delete;
    AnimationRegistry(AnimationRegistry&&) noexcept;
    AnimationRegistry& operator=(AnimationRegistry&&) noexcept;

    SkeletonRef addSkeleton(SkeletonDesc desc);
    ClipId addClip(ClipDesc desc);

    bool isValid(SkeletonRef skeleton) const noexcept;
    bool isValid(ClipId clip) const noexcept;

    const SkeletonDesc* getSkeleton(SkeletonRef skeleton) const noexcept;
    const ClipDesc* getClip(ClipId clip) const noexcept;

private:
    struct SkeletonSlot {
        SkeletonDesc desc;
        std::uint32_t generation = 0;
        bool alive = false;
    };
    struct ClipSlot {
        ClipDesc desc;
        bool alive = false;
    };

    std::vector<SkeletonSlot> skeletons_;
    std::vector<ClipSlot> clips_;
};

} // namespace threadmaxx::animation
