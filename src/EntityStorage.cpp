/// @file EntityStorage.cpp
/// Archetype-backed entity storage (§3.1 batch 6).
///
/// Slots are sparse (indexed by @ref EntityHandle::index); each live
/// slot records the entity's (archetype, row) position in the
/// @ref ArchetypeTable. Mutations (spawn/destroy/migrate) update the
/// chunk and patch any slot whose row moved via swap-and-pop. The
/// legacy flat dense views are reconstructed lazily — `ensureStitched`
/// walks the chunks in creation order and rebuilds the cache; a single
/// mutation marks `stitchedDirty_=true` and the next public accessor
/// pays the rebuild.
///
/// Reservation lifecycle (§3.5) is unchanged:
///   reserveHandle    [workers, locked]    slot.reserved = true
///   materializeReserved [sim thread]      slot.alive    = true, dense_grows
///   discardAllReservations [sim thread]   slot.gen++, slot reused
#include "EntityStorage.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace threadmaxx::internal {

namespace {

constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

} // namespace

EntityStorage::EntityStorage(std::uint32_t initialCapacity) {
    slots_.reserve(initialCapacity);
    table_.reserveFirstChunk(initialCapacity);
}

void EntityStorage::reserve(std::size_t n) {
    slots_.reserve(n);
    table_.reserveFirstChunk(n);
}

EntityHandle EntityStorage::spawn(const Transform& t,
                                  const Velocity& v,
                                  const RenderTag& r,
                                  const UserData& u,
                                  const Acceleration& a,
                                  const Parent& p,
                                  const Health& hp,
                                  const Faction& fac,
                                  const AnimationStateRef& anim,
                                  const PhysicsBodyRef& phys,
                                  const NavAgentRef& nav,
                                  const BoundingVolume& bv,
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
    slot.generation = (slot.generation == 0) ? 1u : slot.generation + 1u;
    slot.alive = true;
    const std::uint32_t archIdx = table_.getOrCreateArchetype(initialMask);
    const EntityHandle h{slotIdx, slot.generation};
    const std::uint32_t row = table_.insert(archIdx, h, slotIdx,
        t, v, r, u, a, p, hp, fac, anim, phys, nav, bv);
    slot.archetypeIndex = archIdx;
    slot.row            = row;
    markDirty();
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
    slot.archetypeIndex = kInvalidIndex;
    slot.row            = kInvalidIndex;
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
        slot.archetypeIndex = kInvalidIndex;
        slot.row            = kInvalidIndex;
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
                                        const Health& hp,
                                        const Faction& fac,
                                        const AnimationStateRef& anim,
                                        const PhysicsBodyRef& phys,
                                        const NavAgentRef& nav,
                                        const BoundingVolume& bv,
                                        ComponentSet initialMask) {
    if (h.index >= slots_.size()) return false;
    Slot& slot = slots_[h.index];
    if (!slot.reserved || slot.generation != h.generation || h.generation == 0) {
        return false;
    }

    slot.reserved   = false;
    slot.alive      = true;
    const std::uint32_t archIdx = table_.getOrCreateArchetype(initialMask);
    const std::uint32_t row = table_.insert(archIdx, h, h.index,
        t, v, r, u, a, p, hp, fac, anim, phys, nav, bv);
    slot.archetypeIndex = archIdx;
    slot.row            = row;

    auto& tracked = reservedHandles_;
    tracked.erase(std::remove(tracked.begin(), tracked.end(), h), tracked.end());
    markDirty();
    return true;
}

void EntityStorage::discardAllReservations() noexcept {
    std::lock_guard<std::mutex> lk(reservationMtx_);
    for (auto h : reservedHandles_) {
        Slot& slot = slots_[h.index];
        slot.generation++;
        slot.reserved   = false;
        slot.alive      = false;
        slot.archetypeIndex = kInvalidIndex;
        slot.row            = kInvalidIndex;
        freeSlots_.push_back(h.index);
    }
    reservedHandles_.clear();
}

bool EntityStorage::destroy(EntityHandle h) noexcept {
    if (!alive(h)) return false;
    Slot& slot = slots_[h.index];
    const std::uint32_t archIdx = slot.archetypeIndex;
    const std::uint32_t row     = slot.row;
    const std::uint32_t swapped = table_.removeSwapPop(archIdx, row);
    if (swapped != kInvalidIndex) {
        slots_[swapped].row = row;
    }
    slot.alive = false;
    slot.archetypeIndex = kInvalidIndex;
    slot.row            = kInvalidIndex;
    freeSlots_.push_back(h.index);
    markDirty();
    return true;
}

bool EntityStorage::alive(EntityHandle h) const noexcept {
    if (h.index >= slots_.size()) return false;
    const Slot& slot = slots_[h.index];
    return slot.alive && slot.generation == h.generation && h.generation != 0;
}

EntityStorage::Location EntityStorage::locate(EntityHandle h) const noexcept {
    if (!alive(h)) return {kInvalidIndex, kInvalidIndex};
    const Slot& slot = slots_[h.index];
    return {slot.archetypeIndex, slot.row};
}

std::uint32_t EntityStorage::indexOf(EntityHandle h) const noexcept {
    if (!alive(h)) return kInvalidIndex;
    ensureStitched();
    const Slot& slot = slots_[h.index];
    if (slot.archetypeIndex >= archetypeStitchStart_.size()) return kInvalidIndex;
    return archetypeStitchStart_[slot.archetypeIndex] + slot.row;
}

bool EntityStorage::setMaskAndMigrate(EntityHandle h, ComponentSet newMask) noexcept {
    if (!alive(h)) return false;
    Slot& slot = slots_[h.index];
    const auto& chunks = table_.chunks();
    if (slot.archetypeIndex >= chunks.size()) return false;
    if (chunks[slot.archetypeIndex].mask == newMask) return true;

    const std::uint32_t srcArch = slot.archetypeIndex;
    const std::uint32_t srcRow  = slot.row;
    const auto res = table_.migrate(srcArch, srcRow, newMask);
    if (res.swappedSlot != kInvalidIndex) {
        slots_[res.swappedSlot].row = srcRow;
    }
    slot.archetypeIndex = res.dstArchetype;
    slot.row            = res.dstRow;
    markDirty();
    return true;
}

// ---- per-handle mutators -------------------------------------------------

Transform* EntityStorage::mutTransform(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::Transform)) return nullptr;
    markDirty();
    return &c.transforms[slot.row];
}
Velocity* EntityStorage::mutVelocity(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::Velocity)) return nullptr;
    markDirty();
    return &c.velocities[slot.row];
}
RenderTag* EntityStorage::mutRenderTag(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::RenderTag)) return nullptr;
    markDirty();
    return &c.renderTags[slot.row];
}
UserData* EntityStorage::mutUserData(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::UserData)) return nullptr;
    markDirty();
    return &c.userData[slot.row];
}
Acceleration* EntityStorage::mutAcceleration(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::Acceleration)) return nullptr;
    markDirty();
    return &c.accelerations[slot.row];
}
Parent* EntityStorage::mutParent(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::Parent)) return nullptr;
    markDirty();
    return &c.parents[slot.row];
}
Health* EntityStorage::mutHealth(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::Health)) return nullptr;
    markDirty();
    return &c.healths[slot.row];
}
Faction* EntityStorage::mutFaction(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::Faction)) return nullptr;
    markDirty();
    return &c.factions[slot.row];
}
AnimationStateRef* EntityStorage::mutAnimationStateRef(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::AnimationStateRef)) return nullptr;
    markDirty();
    return &c.animationStates[slot.row];
}
PhysicsBodyRef* EntityStorage::mutPhysicsBodyRef(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::PhysicsBodyRef)) return nullptr;
    markDirty();
    return &c.physicsBodies[slot.row];
}
NavAgentRef* EntityStorage::mutNavAgentRef(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::NavAgentRef)) return nullptr;
    markDirty();
    return &c.navAgents[slot.row];
}
BoundingVolume* EntityStorage::mutBoundingVolume(EntityHandle h) noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    auto& c = table_.chunks()[slot.archetypeIndex];
    if (!c.mask.has(Component::BoundingVolume)) return nullptr;
    markDirty();
    return &c.boundingVolumes[slot.row];
}

const ComponentSet* EntityStorage::tryGetComponentMask(EntityHandle h) const noexcept {
    if (!alive(h)) return nullptr;
    const Slot& slot = slots_[h.index];
    const auto& c = table_.chunks()[slot.archetypeIndex];
    return &c.masks[slot.row];
}

// ---- stitched views ------------------------------------------------------

void EntityStorage::ensureStitched() const noexcept {
    if (!stitchedDirty_.load(std::memory_order_relaxed)) return;

    const auto& chunks = table_.chunks();
    archetypeStitchStart_.assign(chunks.size(), 0);
    std::size_t total = 0;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        archetypeStitchStart_[i] = static_cast<std::uint32_t>(total);
        total += chunks[i].entities.size();
    }

    stitchedEntities_.resize(total);
    stitchedTransforms_.resize(total);
    stitchedVelocities_.resize(total);
    stitchedRenderTags_.resize(total);
    stitchedUserData_.resize(total);
    stitchedAccelerations_.resize(total);
    stitchedParents_.resize(total);
    stitchedHealths_.resize(total);
    stitchedFactions_.resize(total);
    stitchedAnimationStates_.resize(total);
    stitchedPhysicsBodies_.resize(total);
    stitchedNavAgents_.resize(total);
    stitchedBoundingVolumes_.resize(total);
    stitchedMasks_.resize(total);

    std::size_t cursor = 0;
    for (const auto& c : chunks) {
        const std::size_t n = c.entities.size();
        if (n == 0) continue;
        std::copy(c.entities.begin(), c.entities.end(),
                  stitchedEntities_.begin() + static_cast<std::ptrdiff_t>(cursor));
        std::copy(c.masks.begin(), c.masks.end(),
                  stitchedMasks_.begin() + static_cast<std::ptrdiff_t>(cursor));
        // Per-component: if chunk carries it, copy values; otherwise
        // default-initialize the stitched slots so the legacy "parallel
        // arrays of size world.size()" contract still holds.
        auto fill = [&](auto& dst, auto src, bool present, auto defaultVal) {
            using V = typename std::decay_t<decltype(defaultVal)>;
            if (present) {
                std::copy(src.begin(), src.end(),
                          dst.begin() + static_cast<std::ptrdiff_t>(cursor));
            } else {
                std::fill(dst.begin() + static_cast<std::ptrdiff_t>(cursor),
                          dst.begin() + static_cast<std::ptrdiff_t>(cursor + n),
                          V{});
            }
        };
        fill(stitchedTransforms_,      std::span<const Transform>(c.transforms),         c.mask.has(Component::Transform),         Transform{});
        fill(stitchedVelocities_,      std::span<const Velocity>(c.velocities),          c.mask.has(Component::Velocity),          Velocity{});
        fill(stitchedRenderTags_,      std::span<const RenderTag>(c.renderTags),         c.mask.has(Component::RenderTag),         RenderTag{});
        fill(stitchedUserData_,        std::span<const UserData>(c.userData),            c.mask.has(Component::UserData),          UserData{});
        fill(stitchedAccelerations_,   std::span<const Acceleration>(c.accelerations),   c.mask.has(Component::Acceleration),      Acceleration{});
        fill(stitchedParents_,         std::span<const Parent>(c.parents),               c.mask.has(Component::Parent),            Parent{});
        fill(stitchedHealths_,         std::span<const Health>(c.healths),               c.mask.has(Component::Health),            Health{});
        fill(stitchedFactions_,        std::span<const Faction>(c.factions),             c.mask.has(Component::Faction),           Faction{});
        fill(stitchedAnimationStates_, std::span<const AnimationStateRef>(c.animationStates), c.mask.has(Component::AnimationStateRef), AnimationStateRef{});
        fill(stitchedPhysicsBodies_,   std::span<const PhysicsBodyRef>(c.physicsBodies), c.mask.has(Component::PhysicsBodyRef),    PhysicsBodyRef{});
        fill(stitchedNavAgents_,       std::span<const NavAgentRef>(c.navAgents),        c.mask.has(Component::NavAgentRef),       NavAgentRef{});
        fill(stitchedBoundingVolumes_, std::span<const BoundingVolume>(c.boundingVolumes), c.mask.has(Component::BoundingVolume),  BoundingVolume{});
        cursor += n;
    }
    stitchedDirty_.store(false, std::memory_order_relaxed);
}

std::span<const EntityHandle> EntityStorage::entities() const noexcept {
    ensureStitched();
    return {stitchedEntities_.data(), stitchedEntities_.size()};
}
std::span<const Transform> EntityStorage::transforms() const noexcept {
    ensureStitched();
    return {stitchedTransforms_.data(), stitchedTransforms_.size()};
}
std::span<const Velocity> EntityStorage::velocities() const noexcept {
    ensureStitched();
    return {stitchedVelocities_.data(), stitchedVelocities_.size()};
}
std::span<const RenderTag> EntityStorage::renderTags() const noexcept {
    ensureStitched();
    return {stitchedRenderTags_.data(), stitchedRenderTags_.size()};
}
std::span<const UserData> EntityStorage::userData() const noexcept {
    ensureStitched();
    return {stitchedUserData_.data(), stitchedUserData_.size()};
}
std::span<const Acceleration> EntityStorage::accelerations() const noexcept {
    ensureStitched();
    return {stitchedAccelerations_.data(), stitchedAccelerations_.size()};
}
std::span<const Parent> EntityStorage::parents() const noexcept {
    ensureStitched();
    return {stitchedParents_.data(), stitchedParents_.size()};
}
std::span<const Health> EntityStorage::healths() const noexcept {
    ensureStitched();
    return {stitchedHealths_.data(), stitchedHealths_.size()};
}
std::span<const Faction> EntityStorage::factions() const noexcept {
    ensureStitched();
    return {stitchedFactions_.data(), stitchedFactions_.size()};
}
std::span<const AnimationStateRef> EntityStorage::animationStates() const noexcept {
    ensureStitched();
    return {stitchedAnimationStates_.data(), stitchedAnimationStates_.size()};
}
std::span<const PhysicsBodyRef> EntityStorage::physicsBodies() const noexcept {
    ensureStitched();
    return {stitchedPhysicsBodies_.data(), stitchedPhysicsBodies_.size()};
}
std::span<const NavAgentRef> EntityStorage::navAgents() const noexcept {
    ensureStitched();
    return {stitchedNavAgents_.data(), stitchedNavAgents_.size()};
}
std::span<const BoundingVolume> EntityStorage::boundingVolumes() const noexcept {
    ensureStitched();
    return {stitchedBoundingVolumes_.data(), stitchedBoundingVolumes_.size()};
}
std::span<const ComponentSet> EntityStorage::componentMasks() const noexcept {
    ensureStitched();
    return {stitchedMasks_.data(), stitchedMasks_.size()};
}

} // namespace threadmaxx::internal
