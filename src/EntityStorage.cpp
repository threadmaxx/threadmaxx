/// @file EntityStorage.cpp
/// Dense parallel-array storage for entities + the swap-and-pop destroy
/// path.
///
/// Invariant: every component vector has exactly `entities_.size()`
/// elements. The `denseToSlot_` array is the inverse mapping: dense row
/// `i` corresponds to slot `denseToSlot_[i]`. On `destroy(handle)` we
/// pop the back of every parallel array and re-point the moved row's
/// slot at the new dense index; if you add a new built-in component,
/// you MUST add a swap-and-pop branch here or the arrays go out of sync.
///
/// Reservation lifecycle (§3.5):
///   reserveHandle    [workers, locked]    slot.reserved = true
///   materializeReserved [sim thread]      slot.alive    = true, dense_grows
///   discardAllReservations [sim thread]   slot.gen++, slot reused
#include "EntityStorage.hpp"

#include <algorithm>
#include <limits>
#include <span>

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
    parents_.reserve(initialCapacity);
    masks_.reserve(initialCapacity);
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
    parents_.reserve(n);
    masks_.reserve(n);
}

EntityHandle EntityStorage::spawn(const Transform& t,
                                  const Velocity& v,
                                  const RenderTag& r,
                                  const UserData& u,
                                  const Acceleration& a,
                                  const Parent& p,
                                  ComponentSet initialMask) {
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
    parents_.push_back(p);
    masks_.push_back(initialMask);
    return h;
}

EntityHandle EntityStorage::reserveHandle() {
    std::lock_guard<std::mutex> lk(reservationMtx_);
    std::uint32_t slotIdx;
    if (!freeSlots_.empty()) {
        slotIdx = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        slotIdx = static_cast<std::uint32_t>(slots_.size());
        slots_.emplace_back();
    }
    Slot& slot = slots_[slotIdx];
    slot.generation = (slot.generation == 0) ? 1u : slot.generation + 1u;
    slot.reserved   = true;
    slot.alive      = false;
    slot.denseIndex = std::numeric_limits<std::uint32_t>::max();
    const EntityHandle h{slotIdx, slot.generation};
    reservedHandles_.push_back(h);
    return h;
}

void EntityStorage::reserveHandles(std::uint32_t count,
                                   std::span<EntityHandle> out) {
    if (count == 0) return;
    std::lock_guard<std::mutex> lk(reservationMtx_);
    const std::uint32_t limit = std::min(
        count, static_cast<std::uint32_t>(out.size()));
    for (std::uint32_t i = 0; i < limit; ++i) {
        std::uint32_t slotIdx;
        if (!freeSlots_.empty()) {
            slotIdx = freeSlots_.back();
            freeSlots_.pop_back();
        } else {
            slotIdx = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[slotIdx];
        slot.generation = (slot.generation == 0) ? 1u : slot.generation + 1u;
        slot.reserved   = true;
        slot.alive      = false;
        slot.denseIndex = std::numeric_limits<std::uint32_t>::max();
        const EntityHandle h{slotIdx, slot.generation};
        reservedHandles_.push_back(h);
        out[i] = h;
    }
}

bool EntityStorage::materializeReserved(EntityHandle h,
                                        const Transform& t,
                                        const Velocity& v,
                                        const RenderTag& r,
                                        const UserData& u,
                                        const Acceleration& a,
                                        const Parent& p,
                                        ComponentSet initialMask) {
    if (h.index >= slots_.size()) return false;
    Slot& slot = slots_[h.index];
    if (!slot.reserved || slot.generation != h.generation || h.generation == 0) {
        return false;
    }

    slot.reserved   = false;
    slot.alive      = true;
    slot.denseIndex = static_cast<std::uint32_t>(entities_.size());

    denseToSlot_.push_back(h.index);
    entities_.push_back(h);
    transforms_.push_back(t);
    velocities_.push_back(v);
    renderTags_.push_back(r);
    userData_.push_back(u);
    accelerations_.push_back(a);
    parents_.push_back(p);
    masks_.push_back(initialMask);

    // Remove the consumed reservation from the tracking list. The list
    // is small (handful of items per tick), so linear scan is fine.
    auto& tracked = reservedHandles_;
    tracked.erase(std::remove(tracked.begin(), tracked.end(), h), tracked.end());
    return true;
}

void EntityStorage::discardAllReservations() noexcept {
    std::lock_guard<std::mutex> lk(reservationMtx_);
    for (auto h : reservedHandles_) {
        // Bump generation so the user's handle stops validating, then
        // return the slot to the free list for reuse next tick.
        Slot& slot = slots_[h.index];
        slot.generation++;
        slot.reserved   = false;
        slot.alive      = false;
        slot.denseIndex = std::numeric_limits<std::uint32_t>::max();
        freeSlots_.push_back(h.index);
    }
    reservedHandles_.clear();
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
        parents_       [deadDense] = parents_       [lastDense];
        masks_         [deadDense] = masks_         [lastDense];
        denseToSlot_   [deadDense] = denseToSlot_   [lastDense];
        slots_[denseToSlot_[deadDense]].denseIndex = deadDense;
    }

    entities_.pop_back();
    transforms_.pop_back();
    velocities_.pop_back();
    renderTags_.pop_back();
    userData_.pop_back();
    accelerations_.pop_back();
    parents_.pop_back();
    masks_.pop_back();
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
Parent* EntityStorage::mutParent(EntityHandle h) noexcept {
    const auto i = indexOf(h);
    return i == std::numeric_limits<std::uint32_t>::max() ? nullptr : &parents_[i];
}
ComponentSet* EntityStorage::mutComponentMask(EntityHandle h) noexcept {
    const auto i = indexOf(h);
    return i == std::numeric_limits<std::uint32_t>::max() ? nullptr : &masks_[i];
}

} // namespace threadmaxx::internal
