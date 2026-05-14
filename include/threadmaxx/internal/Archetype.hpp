#pragma once

#include "threadmaxx/Components.hpp"
#include "threadmaxx/Handles.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace threadmaxx::internal {

class UserComponentRegistry;

/// One runtime-registered user component's slice of an
/// @ref ArchetypeChunk. `bytes` is the contiguous packed dense array:
/// `bytes.size() == row_count * stride`. The chunk's
/// @ref ArchetypeChunk::entities and @ref ArchetypeChunk::denseToSlot
/// vectors are parallel to this storage — row `r` here corresponds to
/// row `r` in the chunk.
///
/// `bit` is the @ref ComponentSet bit assigned to the type
/// (>= 16 for user types; see @ref UserComponentRegistry). `stride` is
/// the type's `sizeof(T)` captured at registration time. User types are
/// required to be trivially copyable; migrations and swap-and-pop use
/// raw `memcpy`.
struct UserComponentColumn {
    std::uint32_t          bit    = 0;
    std::uint32_t          stride = 0;
    std::vector<std::byte> bytes;

    std::byte*       rowPtr(std::uint32_t row)       noexcept {
        return bytes.data() + static_cast<std::size_t>(row) * stride;
    }
    const std::byte* rowPtr(std::uint32_t row) const noexcept {
        return bytes.data() + static_cast<std::size_t>(row) * stride;
    }
    std::uint32_t rowCount() const noexcept {
        return stride == 0 ? 0u
            : static_cast<std::uint32_t>(bytes.size() / stride);
    }
};

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

    /// §3.1 batch 6b: user-registered dense columns, one per user-
    /// component bit in @c mask. Order is registration order — bits
    /// always appear in ascending order. Empty when the chunk's mask
    /// has no user bits set.
    std::vector<UserComponentColumn> userColumns;

    /// True iff the chunk's archetype mask carries component @p c. Used
    /// to gate dense-vector access on the few components that ride in
    /// archetype-mask-determined storage rather than always-present.
    bool hasComponent(Component c) const noexcept { return mask.has(c); }

    /// Number of live entities in this chunk.
    std::uint32_t size() const noexcept {
        return static_cast<std::uint32_t>(entities.size());
    }

    /// @returns Pointer to the user-component column for @p bit, or
    ///          nullptr when the chunk's mask doesn't carry the bit.
    ///          O(userColumns.size()) — typically 0–4 entries.
    UserComponentColumn* findUserColumn(std::uint32_t bit) noexcept {
        for (auto& c : userColumns) if (c.bit == bit) return &c;
        return nullptr;
    }
    const UserComponentColumn* findUserColumn(std::uint32_t bit) const noexcept {
        for (const auto& c : userColumns) if (c.bit == bit) return &c;
        return nullptr;
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

    /// Install the engine's user-component registry pointer. Newly-
    /// created chunks consult it to size their @ref UserComponentColumn
    /// entries; existing chunks are untouched. Callable any number of
    /// times, but the engine wires it once at construction. Passing
    /// nullptr disables user-column instantiation (every user-bit in a
    /// new mask is silently dropped, which is what tests that never call
    /// `registerUserComponent` get for free).
    void setUserComponentRegistry(const UserComponentRegistry* reg) noexcept {
        registry_ = reg;
    }

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
    // Non-owning pointer to the engine's user-component registry. Used
    // when materializing a fresh chunk to instantiate its
    // UserComponentColumn entries. May be nullptr (then user bits are
    // dropped silently).
    const UserComponentRegistry* registry_ = nullptr;
};

} // namespace threadmaxx::internal
