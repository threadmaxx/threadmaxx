#include "threadmaxx_animation/registry.hpp"

#include <utility>

namespace threadmaxx::animation {

namespace {

// SkeletonId / ClipId encoding: low 32 bits are the slot index;
// high 32 bits are the generation tag for skeleton ids. Clip ids
// have no generation tag in A1 — A2+ revisits if clip removal lands.
constexpr std::uint64_t makeSkeletonId(std::uint32_t index, std::uint32_t generation) noexcept {
    return (static_cast<std::uint64_t>(generation) << 32) |
           static_cast<std::uint64_t>(index);
}

constexpr std::uint32_t skeletonIndex(SkeletonId id) noexcept {
    return static_cast<std::uint32_t>(id & 0xFFFFFFFFu);
}

constexpr std::uint32_t skeletonGen(SkeletonId id) noexcept {
    return static_cast<std::uint32_t>(id >> 32);
}

} // namespace

AnimationRegistry::AnimationRegistry() = default;
AnimationRegistry::~AnimationRegistry() = default;
AnimationRegistry::AnimationRegistry(AnimationRegistry&&) noexcept = default;
AnimationRegistry& AnimationRegistry::operator=(AnimationRegistry&&) noexcept = default;

SkeletonRef AnimationRegistry::addSkeleton(SkeletonDesc desc) {
    const auto index = static_cast<std::uint32_t>(skeletons_.size());
    SkeletonSlot slot;
    slot.desc = std::move(desc);
    slot.generation = 1;
    slot.alive = true;
    skeletons_.push_back(std::move(slot));
    return SkeletonRef{makeSkeletonId(index, 1), 1};
}

ClipId AnimationRegistry::addClip(ClipDesc desc) {
    const auto index = static_cast<std::uint32_t>(clips_.size());
    ClipSlot slot;
    slot.desc = std::move(desc);
    slot.alive = true;
    clips_.push_back(std::move(slot));
    return static_cast<ClipId>(index);
}

bool AnimationRegistry::isValid(SkeletonRef skeleton) const noexcept {
    const std::uint32_t idx = skeletonIndex(skeleton.id);
    if (idx >= skeletons_.size()) return false;
    const auto& s = skeletons_[idx];
    return s.alive && s.generation == skeleton.generation &&
           s.generation == skeletonGen(skeleton.id);
}

bool AnimationRegistry::isValid(ClipId clip) const noexcept {
    if (clip >= clips_.size()) return false;
    return clips_[static_cast<std::size_t>(clip)].alive;
}

const SkeletonDesc* AnimationRegistry::getSkeleton(SkeletonRef skeleton) const noexcept {
    if (!isValid(skeleton)) return nullptr;
    return &skeletons_[skeletonIndex(skeleton.id)].desc;
}

const ClipDesc* AnimationRegistry::getClip(ClipId clip) const noexcept {
    if (!isValid(clip)) return nullptr;
    return &clips_[static_cast<std::size_t>(clip)].desc;
}

} // namespace threadmaxx::animation
