/// @file Archetype.cpp
/// Per-archetype dense storage. One @ref threadmaxx::internal::ArchetypeChunk
/// per unique @ref threadmaxx::ComponentSet — entities live in exactly
/// one chunk at a time, and component vectors are only allocated for
/// the bits in the chunk's mask.
///
/// Maintainer reading order:
///   - `insert` is the spawn path: chunk vectors grow in lockstep,
///      values for absent components are dropped.
///   - `removeSwapPop` is the destroy path: swap the back row into the
///      freed row, then patch the swapped-in entity's slot via the
///      returned slot index.
///   - `migrate` chains both: insert the entity into the destination
///      archetype first (so a self-mask change is well-defined), then
///      pop it out of the source.
#include "threadmaxx/internal/Archetype.hpp"
#include "UserComponentRegistry.hpp"

#include <cstring>
#include <limits>

namespace threadmaxx::internal {

namespace {

constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

} // namespace

ArchetypeTable::ArchetypeTable() {
    // Eagerly create the "everything" archetype for the legacy
    // default-mask spawn path. Phase 2 will fan out into per-mask
    // chunks; pre-creating bit 0xFFFF keeps the test suite's
    // ComponentSet::all()-using paths aligned with the common case.
    getOrCreateArchetype(ComponentSet::all());
}

std::uint32_t ArchetypeTable::findArchetype(ComponentSet mask) const noexcept {
    const auto it = maskToIndex_.find(mask.bits());
    if (it == maskToIndex_.end()) return kInvalidIndex;
    return it->second;
}

std::uint32_t ArchetypeTable::getOrCreateArchetype(ComponentSet mask) {
    if (const auto idx = findArchetype(mask); idx != kInvalidIndex) return idx;
    const auto idx = static_cast<std::uint32_t>(chunks_.size());
    chunks_.emplace_back();
    auto& chunk = chunks_.back();
    chunk.mask = mask;
    maskToIndex_.emplace(mask.bits(), idx);
    // §3.1 batch 6b: any user-component bit in the mask (bits ≥ 16)
    // gets a UserComponentColumn instantiated up front, so insert/
    // removeSwapPop/migrate can blindly walk the columns without
    // checking registry every time. Strides come from the registry —
    // unregistered bits are dropped (column omitted).
    if (registry_) {
        for (std::uint32_t bit = 16; bit < 64; ++bit) {
            const std::uint64_t bitMask = 1ull << bit;
            if ((mask.bits() & bitMask) == 0) continue;
            const std::uint32_t stride = registry_->strideFor(bit);
            if (stride == 0) continue;
            chunk.userColumns.push_back(
                UserComponentColumn{bit, stride, {}});
        }
    }
    return idx;
}

void ArchetypeTable::reserveFirstChunk(std::size_t n) {
    if (chunks_.empty()) return;
    auto& c = chunks_.front();
    c.denseToSlot.reserve(n);
    c.entities.reserve(n);
    c.transforms.reserve(n);
    c.velocities.reserve(n);
    c.renderTags.reserve(n);
    c.userData.reserve(n);
    c.accelerations.reserve(n);
    c.parents.reserve(n);
    c.healths.reserve(n);
    c.factions.reserve(n);
    c.animationStates.reserve(n);
    c.physicsBodies.reserve(n);
    c.navAgents.reserve(n);
    c.boundingVolumes.reserve(n);
    c.masks.reserve(n);
}

void ArchetypeTable::reserveChunkRows(ComponentSet dstMask, std::size_t extra) {
    if (extra == 0) return;
    const std::uint32_t arch = getOrCreateArchetype(dstMask);
    auto& c = chunks_[arch];
    const std::size_t target = c.entities.size() + extra;
    c.denseToSlot.reserve(target);
    c.entities.reserve(target);
    c.masks.reserve(target);
    // Only the component vectors whose bits are in the mask are
    // physically allocated; reserving the others is a no-op cost-wise
    // because they stay zero-sized.
    if (dstMask.has(Component::Transform))          c.transforms.reserve(target);
    if (dstMask.has(Component::Velocity))           c.velocities.reserve(target);
    if (dstMask.has(Component::RenderTag))          c.renderTags.reserve(target);
    if (dstMask.has(Component::UserData))           c.userData.reserve(target);
    if (dstMask.has(Component::Acceleration))       c.accelerations.reserve(target);
    if (dstMask.has(Component::Parent))             c.parents.reserve(target);
    if (dstMask.has(Component::Health))             c.healths.reserve(target);
    if (dstMask.has(Component::Faction))            c.factions.reserve(target);
    if (dstMask.has(Component::AnimationStateRef))  c.animationStates.reserve(target);
    if (dstMask.has(Component::PhysicsBodyRef))     c.physicsBodies.reserve(target);
    if (dstMask.has(Component::NavAgentRef))        c.navAgents.reserve(target);
    if (dstMask.has(Component::BoundingVolume))     c.boundingVolumes.reserve(target);
}

std::uint32_t ArchetypeTable::insert(std::uint32_t archetypeIndex,
                                     EntityHandle handle,
                                     std::uint32_t slotIndex,
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
                                     const BoundingVolume& bv) {
    auto& c = chunks_[archetypeIndex];
    const auto row = static_cast<std::uint32_t>(c.entities.size());
    c.denseToSlot.push_back(slotIndex);
    c.entities.push_back(handle);
    c.masks.push_back(c.mask);
    if (c.mask.has(Component::Transform))         c.transforms.push_back(t);
    if (c.mask.has(Component::Velocity))          c.velocities.push_back(v);
    if (c.mask.has(Component::RenderTag))         c.renderTags.push_back(r);
    if (c.mask.has(Component::UserData))          c.userData.push_back(u);
    if (c.mask.has(Component::Acceleration))      c.accelerations.push_back(a);
    if (c.mask.has(Component::Parent))            c.parents.push_back(p);
    if (c.mask.has(Component::Health))            c.healths.push_back(hp);
    if (c.mask.has(Component::Faction))           c.factions.push_back(fac);
    if (c.mask.has(Component::AnimationStateRef)) c.animationStates.push_back(anim);
    if (c.mask.has(Component::PhysicsBodyRef))    c.physicsBodies.push_back(phys);
    if (c.mask.has(Component::NavAgentRef))       c.navAgents.push_back(nav);
    if (c.mask.has(Component::BoundingVolume))    c.boundingVolumes.push_back(bv);
    // §3.1 batch 6b: each user column grows by `stride` zero-filled
    // bytes. The actual value (if any) is written separately — either
    // by migrate() copying the src bytes, or by the commit handler for
    // CmdAddUserComponent writing the user-supplied blob.
    for (auto& col : c.userColumns) {
        const std::size_t old = col.bytes.size();
        col.bytes.resize(old + col.stride, std::byte{0});
    }
    // §3.6 batch 30 — chunk content changed; the cachedHash is stale.
    c.hashDirty = true;
    return row;
}

std::uint32_t ArchetypeTable::removeSwapPop(std::uint32_t archetypeIndex,
                                            std::uint32_t row) noexcept {
    auto& c = chunks_[archetypeIndex];
    const auto last = static_cast<std::uint32_t>(c.entities.size() - 1u);
    std::uint32_t swappedSlot = kInvalidIndex;
    if (row != last) {
        c.entities    [row] = c.entities    [last];
        c.denseToSlot [row] = c.denseToSlot [last];
        c.masks       [row] = c.masks       [last];
        if (c.mask.has(Component::Transform))         c.transforms     [row] = c.transforms     [last];
        if (c.mask.has(Component::Velocity))          c.velocities     [row] = c.velocities     [last];
        if (c.mask.has(Component::RenderTag))         c.renderTags     [row] = c.renderTags     [last];
        if (c.mask.has(Component::UserData))          c.userData       [row] = c.userData       [last];
        if (c.mask.has(Component::Acceleration))      c.accelerations  [row] = c.accelerations  [last];
        if (c.mask.has(Component::Parent))            c.parents        [row] = c.parents        [last];
        if (c.mask.has(Component::Health))            c.healths        [row] = c.healths        [last];
        if (c.mask.has(Component::Faction))           c.factions       [row] = c.factions       [last];
        if (c.mask.has(Component::AnimationStateRef)) c.animationStates[row] = c.animationStates[last];
        if (c.mask.has(Component::PhysicsBodyRef))    c.physicsBodies  [row] = c.physicsBodies  [last];
        if (c.mask.has(Component::NavAgentRef))       c.navAgents      [row] = c.navAgents      [last];
        if (c.mask.has(Component::BoundingVolume))    c.boundingVolumes[row] = c.boundingVolumes[last];
        // §3.1 batch 6b: same swap-and-pop for every user column.
        for (auto& col : c.userColumns) {
            std::memcpy(col.rowPtr(row), col.rowPtr(last), col.stride);
        }
        swappedSlot = c.denseToSlot[row];
    }
    c.entities.pop_back();
    c.denseToSlot.pop_back();
    c.masks.pop_back();
    if (c.mask.has(Component::Transform))         c.transforms.pop_back();
    if (c.mask.has(Component::Velocity))          c.velocities.pop_back();
    if (c.mask.has(Component::RenderTag))         c.renderTags.pop_back();
    if (c.mask.has(Component::UserData))          c.userData.pop_back();
    if (c.mask.has(Component::Acceleration))      c.accelerations.pop_back();
    if (c.mask.has(Component::Parent))            c.parents.pop_back();
    if (c.mask.has(Component::Health))            c.healths.pop_back();
    if (c.mask.has(Component::Faction))           c.factions.pop_back();
    if (c.mask.has(Component::AnimationStateRef)) c.animationStates.pop_back();
    if (c.mask.has(Component::PhysicsBodyRef))    c.physicsBodies.pop_back();
    if (c.mask.has(Component::NavAgentRef))       c.navAgents.pop_back();
    if (c.mask.has(Component::BoundingVolume))    c.boundingVolumes.pop_back();
    for (auto& col : c.userColumns) {
        col.bytes.resize(col.bytes.size() - col.stride);
    }
    // §3.6 batch 30 — chunk content changed; the cachedHash is stale.
    c.hashDirty = true;
    return swappedSlot;
}

ArchetypeTable::MigrationResult ArchetypeTable::migrate(
        std::uint32_t srcArch, std::uint32_t srcRow, ComponentSet newMask) {
    MigrationResult res;
    if (srcArch >= chunks_.size()) return res;
    const std::uint32_t dstArch = getOrCreateArchetype(newMask);
    // getOrCreateArchetype may have invalidated `srcChunk` (vector grew)
    // — fetch references AFTER it returns.
    ArchetypeChunk& src = chunks_[srcArch];
    if (srcRow >= src.entities.size()) return res;
    const EntityHandle handle = src.entities[srcRow];
    const std::uint32_t slotIdx = src.denseToSlot[srcRow];

    // Snapshot every component value the source has — components the
    // destination DOESN'T carry are dropped, components the source
    // didn't carry are default-initialized.
    Transform         t   = src.mask.has(Component::Transform)         ? src.transforms     [srcRow] : Transform{};
    Velocity          v   = src.mask.has(Component::Velocity)          ? src.velocities     [srcRow] : Velocity{};
    RenderTag         r   = src.mask.has(Component::RenderTag)         ? src.renderTags     [srcRow] : RenderTag{};
    UserData          u   = src.mask.has(Component::UserData)          ? src.userData       [srcRow] : UserData{};
    Acceleration      a   = src.mask.has(Component::Acceleration)      ? src.accelerations  [srcRow] : Acceleration{};
    Parent            p   = src.mask.has(Component::Parent)            ? src.parents        [srcRow] : Parent{};
    Health            hp  = src.mask.has(Component::Health)            ? src.healths        [srcRow] : Health{};
    Faction           fac = src.mask.has(Component::Faction)           ? src.factions       [srcRow] : Faction{};
    AnimationStateRef anim= src.mask.has(Component::AnimationStateRef) ? src.animationStates[srcRow] : AnimationStateRef{};
    PhysicsBodyRef    phy = src.mask.has(Component::PhysicsBodyRef)    ? src.physicsBodies  [srcRow] : PhysicsBodyRef{};
    NavAgentRef       nv  = src.mask.has(Component::NavAgentRef)       ? src.navAgents      [srcRow] : NavAgentRef{};
    BoundingVolume    bv  = src.mask.has(Component::BoundingVolume)    ? src.boundingVolumes[srcRow] : BoundingVolume{};

    // §3.1 batch 6b: snapshot any user-column rows whose bit is also in
    // newMask. Bits the destination does not carry are dropped on the
    // ground (their values vanish); bits the source did not carry will
    // arrive as zero bytes courtesy of insert().
    struct UserCarry {
        std::uint32_t              bit;
        std::vector<std::byte>     blob;
    };
    std::vector<UserCarry> carries;
    for (const auto& col : src.userColumns) {
        const auto bitMask = 1ull << col.bit;
        if ((newMask.bits() & bitMask) == 0) continue;
        UserCarry uc;
        uc.bit = col.bit;
        uc.blob.assign(col.rowPtr(srcRow), col.rowPtr(srcRow) + col.stride);
        carries.push_back(std::move(uc));
    }

    const std::uint32_t dstRow = insert(dstArch, handle, slotIdx,
        t, v, r, u, a, p, hp, fac, anim, phy, nv, bv);
    // Now write the carried user-column blobs into dst's new row.
    // chunks_[dstArch] is re-fetched because insert appended.
    {
        auto& dst = chunks_[dstArch];
        for (const auto& uc : carries) {
            if (auto* col = dst.findUserColumn(uc.bit)) {
                std::memcpy(col->rowPtr(dstRow), uc.blob.data(), col->stride);
            }
        }
    }
    // Re-fetch src reference after `insert` may have grown chunks_[dstArch]
    // (which is the same vector when src==dst); the src reference is
    // potentially invalid if src==dst and capacity changed.
    res.swappedSlot = removeSwapPop(srcArch, srcRow);
    // If src and dst are the same archetype and we inserted then popped
    // the very row we just inserted, the swap pulls the inserted row
    // backwards into srcRow. The caller knows the new row via dstRow,
    // which still refers to the right entity AFTER the pop because
    // pop always operates on the source row, never on the destination.
    // (insert appends; removeSwapPop pops srcRow which is != dstRow when
    // src == dst because dstRow > srcRow.)
    res.dstArchetype = dstArch;
    res.dstRow       = dstRow;
    // BUT: when src == dst, dstRow was originally `size()-1` (insert
    // appended at the end). The subsequent removeSwapPop on srcRow
    // moves the last row into srcRow — if dstRow was that last row,
    // then after the swap-and-pop, the destination row is now srcRow.
    if (srcArch == dstArch) {
        // Insert appended at index oldSize, then we pop srcRow (< oldSize):
        // - If srcRow == oldSize - ?  Actually srcRow predated the insert,
        //   so srcRow < oldSize. The inserted entity now sits at oldSize.
        // - removeSwapPop(srcRow) pulls index `oldSize` (== the last row
        //   after insert) into srcRow, then pops. So the inserted entity
        //   ends up at srcRow.
        res.dstRow = srcRow;
        // The swap that moved our inserted entity into srcRow ALSO
        // reports `swappedSlot = denseToSlot[srcRow]` which is now our
        // own slot. The caller will update its slot record to (dstArch,
        // srcRow) — and won't reread swappedSlot as a separate entity,
        // because we sentinel it here.
        res.swappedSlot = kInvalidIndex;
    }
    return res;
}

std::size_t ArchetypeTable::totalEntities() const noexcept {
    std::size_t total = 0;
    for (const auto& c : chunks_) total += c.entities.size();
    return total;
}

} // namespace threadmaxx::internal
