#include "EntityStorage.hpp"

#include <limits>
#include <utility>

namespace threadmaxx::internal {

EntityStorage::EntityStorage(std::uint32_t initialCapacity) {
    slots_.reserve(initialCapacity);
    denseToSlot_.reserve(initialCapacity);
    entities_.reserve(initialCapacity);
    transforms_.reserve(initialCapacity);
    velocities_.reserve(initialCapacity);
    renderTags_.reserve(initialCapacity);
    userData_.reserve(initialCapacity);
    accelerations_.reserve(initialCapacity);
}

void EntityStorage::reserve(std::size_t n) {
    slots_.reserve(n);
    denseToSlot_.reserve(n);
    entities_.reserve(n);
    transforms_.reserve(n);
    velocities_.reserve(n);
    renderTags_.reserve(n);
    userData_.reserve(n);
    accelerations_.reserve(n);
}

EntityHandle EntityStorage::spawn(const Transform& t,
                                  const Velocity& v,
                                  const RenderTag& r,
                                  const UserData& u,
                                  const Acceleration& a) {
    std::uint32_t slotIdx;
    if (!freeSlots_.empty()) {
        slotIdx = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        slotIdx = static_cast<std::uint32_t>(slots_.size());
        slots_.emplace_back();
    }

    Slot& slot = slots_[slotIdx];
    // Bump generation; starts at 1 the first time a slot is used.
    slot.generation = (slot.generation == 0) ? 1u : slot.generation + 1u;
    slot.alive = true;
    slot.denseIndex = static_cast<std::uint32_t>(entities_.size());

    const EntityHandle h{slotIdx, slot.generation};
    denseToSlot_.push_back(slotIdx);
    entities_.push_back(h);
    transforms_.push_back(t);
    velocities_.push_back(v);
    renderTags_.push_back(r);
    userData_.push_back(u);
    accelerations_.push_back(a);
    return h;
}

bool EntityStorage::destroy(EntityHandle h) noexcept {
    if (!alive(h)) return false;
    Slot& slot = slots_[h.index];
    const std::uint32_t deadDense = slot.denseIndex;
    const std::uint32_t lastDense = static_cast<std::uint32_t>(entities_.size() - 1);

    if (deadDense != lastDense) {
        // Swap-and-pop: move the last element into the freed dense slot,
        // then update the owning slot's denseIndex.
        entities_      [deadDense] = entities_      [lastDense];
        transforms_    [deadDense] = transforms_    [lastDense];
        velocities_    [deadDense] = velocities_    [lastDense];
        renderTags_    [deadDense] = renderTags_    [lastDense];
        userData_      [deadDense] = userData_      [lastDense];
        accelerations_ [deadDense] = accelerations_ [lastDense];
        denseToSlot_   [deadDense] = denseToSlot_   [lastDense];
        slots_[denseToSlot_[deadDense]].denseIndex = deadDense;
    }

    entities_.pop_back();
    transforms_.pop_back();
    velocities_.pop_back();
    renderTags_.pop_back();
    userData_.pop_back();
    accelerations_.pop_back();
    denseToSlot_.pop_back();

    slot.alive = false;
    slot.denseIndex = std::numeric_limits<std::uint32_t>::max();
    freeSlots_.push_back(h.index);
    return true;
}

bool EntityStorage::alive(EntityHandle h) const noexcept {
    if (h.index >= slots_.size()) return false;
    const Slot& slot = slots_[h.index];
    return slot.alive && slot.generation == h.generation && h.generation != 0;
}

std::uint32_t EntityStorage::indexOf(EntityHandle h) const noexcept {
    if (!alive(h)) return std::numeric_limits<std::uint32_t>::max();
    return slots_[h.index].denseIndex;
}

Transform* EntityStorage::mutTransform(EntityHandle h) noexcept {
    const auto i = indexOf(h);
    return i == std::numeric_limits<std::uint32_t>::max() ? nullptr : &transforms_[i];
}
Velocity* EntityStorage::mutVelocity(EntityHandle h) noexcept {
    const auto i = indexOf(h);
    return i == std::numeric_limits<std::uint32_t>::max() ? nullptr : &velocities_[i];
}
RenderTag* EntityStorage::mutRenderTag(EntityHandle h) noexcept {
    const auto i = indexOf(h);
    return i == std::numeric_limits<std::uint32_t>::max() ? nullptr : &renderTags_[i];
}
UserData* EntityStorage::mutUserData(EntityHandle h) noexcept {
    const auto i = indexOf(h);
    return i == std::numeric_limits<std::uint32_t>::max() ? nullptr : &userData_[i];
}
Acceleration* EntityStorage::mutAcceleration(EntityHandle h) noexcept {
    const auto i = indexOf(h);
    return i == std::numeric_limits<std::uint32_t>::max() ? nullptr : &accelerations_[i];
}

} // namespace threadmaxx::internal
