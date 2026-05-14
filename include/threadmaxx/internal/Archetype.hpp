#pragma once

#include "threadmaxx/Components.hpp"
#include "threadmaxx/Handles.hpp"

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace threadmaxx::internal {

/// One archetype's dense, parallel-array storage.
///
/// All entities in an `ArchetypeChunk` share the same per-entity
/// @ref ComponentSet (the chunk's @c mask). Only the component vectors
/// whose bits appear in @c mask are populated; the others stay empty
/// and unused. The chunk owns the @c denseToSlot map: row @c r in this
/// chunk belongs to slot index @c denseToSlot[r] in the owning
/// @ref EntityStorage.
///
/// Entity ordering within a chunk is spawn order — destroy uses
/// swap-and-pop, so the last-spawned entity slides into a hole. This
/// matches the pre-archetype @ref EntityStorage behavior bit-for-bit
/// for any single archetype, so the determinism stress baseline keeps
/// hashing identically when the world contains only one archetype.
struct ArchetypeChunk {
    ComponentSet                   mask{};
    std::vector<std::uint32_t>     denseToSlot;
    std::vector<EntityHandle>      entities;
    std::vector<Transform>         transforms;
    std::vector<Velocity>          velocities;
    std::vector<RenderTag>         renderTags;
    std::vector<UserData>          userData;
    std::vector<Acceleration>      accelerations;
    std::vector<Parent>            parents;
    std::vector<Health>            healths;
    std::vector<Faction>           factions;
    std::vector<AnimationStateRef> animationStates;
    std::vector<PhysicsBodyRef>    physicsBodies;
    std::vector<NavAgentRef>       navAgents;
    std::vector<BoundingVolume>    boundingVolumes;
    std::vector<ComponentSet>      masks;

    /// True iff the chunk's archetype mask carries component @p c. Used
    /// to gate dense-vector access on the few components that ride in
    /// archetype-mask-determined storage rather than always-present.
    bool hasComponent(Component c) const noexcept { return mask.has(c); }

    /// Number of live entities in this chunk.
    std::uint32_t size() const noexcept {
        return static_cast<std::uint32_t>(entities.size());
    }
};

/// Per-mask collection of @ref ArchetypeChunk instances.
///
/// One chunk per unique @ref ComponentSet mask. Lookups go through a
/// hash map keyed by @c mask.bits() so single-archetype worlds incur
/// one map probe per spawn and zero per query.
class ArchetypeTable {
public:
    ArchetypeTable();

    /// @returns The index of the chunk whose mask equals @p mask,
    ///          creating a fresh chunk if none yet exists.
    std::uint32_t getOrCreateArchetype(ComponentSet mask);

    /// Append a new entity row at the end of the indexed chunk. Returns
    /// the row index at which the entity was placed. The caller is
    /// responsible for storing back the (archetype, row) pair in its
    /// slot record.
    ///
    /// Component values are written into the vectors whose bits appear
    /// in the chunk's mask; values for components not in the mask are
    /// dropped (and physical storage is never allocated for them).
    std::uint32_t insert(std::uint32_t archetypeIndex,
                         EntityHandle handle, std::uint32_t slotIndex,
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
                         const BoundingVolume& bv);

    /// Remove the row at (@p archetypeIndex, @p row) via swap-and-pop.
    ///
    /// @returns The slot index of the entity that moved into @p row, or
    ///          `UINT32_MAX` if the removed row was the last one (no
    ///          swap happened). The caller patches the moved slot's
    ///          row.
    std::uint32_t removeSwapPop(std::uint32_t archetypeIndex,
                                std::uint32_t row) noexcept;

    /// Migrate the entity at (@p srcArch, @p srcRow) into the
    /// archetype whose mask is @p newMask. Equivalent to
    /// @c insert-into-destination followed by @c removeSwapPop-from-source.
    ///
    /// @returns The new (archetypeIndex, row) pair as a pair of two
    ///          @c std::uint32_t. The caller updates the entity's slot
    ///          record and also patches the swapped-into-src-row
    ///          entity's slot via the second return.
    struct MigrationResult {
        std::uint32_t dstArchetype = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t dstRow       = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t swappedSlot  = std::numeric_limits<std::uint32_t>::max();
    };
    MigrationResult migrate(std::uint32_t srcArch, std::uint32_t srcRow,
                            ComponentSet newMask);

    /// Read-only access to the chunk list. Iteration order is creation
    /// order — stable across spawns and destroys; only insertion of a
    /// brand-new mask appends a new chunk.
    const std::vector<ArchetypeChunk>& chunks() const noexcept {
        return chunks_;
    }

    /// Mutable accessor for the commit phase.
    std::vector<ArchetypeChunk>& chunks() noexcept { return chunks_; }

    /// @returns The index of the archetype matching @p mask, or
    ///          `UINT32_MAX` if no such archetype currently exists.
    std::uint32_t findArchetype(ComponentSet mask) const noexcept;

    /// Total live entity count across all chunks.
    std::size_t totalEntities() const noexcept;

    /// Hint: reserve capacity in the first archetype (used at engine
    /// initialize time when the user has provided an
    /// `initialEntityCapacity`).
    void reserveFirstChunk(std::size_t n);

private:
    std::vector<ArchetypeChunk> chunks_;
    // Mask -> chunk index. Hash is just the mask's bits — there's no
    // need for a fancier hash because mask values cluster in the low
    // bits and the underlying map handles collisions.
    std::unordered_map<std::uint64_t, std::uint32_t> maskToIndex_;
};

} // namespace threadmaxx::internal
