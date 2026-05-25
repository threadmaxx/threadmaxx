#pragma once

#include "threadmaxx/Components.hpp"
#include "threadmaxx/Handles.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
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

    /// §3.6 batch 30 — per-archetype hash rollup. The chunk's current
    /// state-hash, lazily refreshed by `EngineImpl::finalizeCommitHash`
    /// at the end of each step when `hashDirty == true`. The FNV-1a-64
    /// offset basis (`0xcbf29ce484222325`) represents an "empty / never
    /// computed" chunk fingerprint; the first time the chunk is touched
    /// and the new-hash path runs, the value is replaced with the real
    /// content hash.
    ///
    /// `mutable` because the end-of-step rollup must be able to refresh
    /// the cache through a const archetype view (e.g. when called
    /// during a `WorldView` read pass).
    mutable std::uint64_t cachedHash = 0xcbf29ce484222325ull;

    /// §3.6 batch 30 — set to `true` whenever the chunk's entity
    /// list / dense arrays / user columns change. `EngineImpl::insert
    /// / removeSwapPop / migrate` mark dirty during commit; the
    /// per-handle setters in `EntityStorage::mut*()` also mark dirty
    /// for in-place writes. Cleared by `finalizeCommitHash` after a
    /// fresh `cachedHash` is computed.
    ///
    /// `std::atomic<bool>` with relaxed ordering. The set-true paths
    /// in the sharded commit's Pass C may race on the same chunk
    /// (S10's row-split puts multiple workers on disjoint rows of one
    /// chunk; all write `true`). The write is idempotent so relaxed
    /// ordering is sufficient; the read in `finalizeCommitHash` is
    /// already synchronized via the `JobLatch` mutex acquire that ends
    /// Pass C.
    mutable std::atomic<bool> hashDirty{true};

    ArchetypeChunk() = default;
    /// Custom copy / move because `std::atomic<bool>` is non-copyable
    /// and non-movable by default; the chunk lives in a
    /// `std::vector<ArchetypeChunk>` whose growth uses moves.
    /// Both transfers load+store the atomic with relaxed ordering —
    /// safe because vector growth happens single-threaded on the sim
    /// thread, outside any commit window.
    ArchetypeChunk(const ArchetypeChunk& o)
        : mask(o.mask),
          denseToSlot(o.denseToSlot),
          entities(o.entities),
          transforms(o.transforms),
          velocities(o.velocities),
          renderTags(o.renderTags),
          userData(o.userData),
          accelerations(o.accelerations),
          parents(o.parents),
          healths(o.healths),
          factions(o.factions),
          animationStates(o.animationStates),
          physicsBodies(o.physicsBodies),
          navAgents(o.navAgents),
          boundingVolumes(o.boundingVolumes),
          masks(o.masks),
          userColumns(o.userColumns),
          cachedHash(o.cachedHash),
          hashDirty(o.hashDirty.load(std::memory_order_relaxed)) {}
    ArchetypeChunk(ArchetypeChunk&& o) noexcept
        : mask(o.mask),
          denseToSlot(std::move(o.denseToSlot)),
          entities(std::move(o.entities)),
          transforms(std::move(o.transforms)),
          velocities(std::move(o.velocities)),
          renderTags(std::move(o.renderTags)),
          userData(std::move(o.userData)),
          accelerations(std::move(o.accelerations)),
          parents(std::move(o.parents)),
          healths(std::move(o.healths)),
          factions(std::move(o.factions)),
          animationStates(std::move(o.animationStates)),
          physicsBodies(std::move(o.physicsBodies)),
          navAgents(std::move(o.navAgents)),
          boundingVolumes(std::move(o.boundingVolumes)),
          masks(std::move(o.masks)),
          userColumns(std::move(o.userColumns)),
          cachedHash(o.cachedHash),
          hashDirty(o.hashDirty.load(std::memory_order_relaxed)) {}
    ArchetypeChunk& operator=(const ArchetypeChunk& o) {
        if (this == &o) return *this;
        mask = o.mask;
        denseToSlot = o.denseToSlot;
        entities = o.entities;
        transforms = o.transforms;
        velocities = o.velocities;
        renderTags = o.renderTags;
        userData = o.userData;
        accelerations = o.accelerations;
        parents = o.parents;
        healths = o.healths;
        factions = o.factions;
        animationStates = o.animationStates;
        physicsBodies = o.physicsBodies;
        navAgents = o.navAgents;
        boundingVolumes = o.boundingVolumes;
        masks = o.masks;
        userColumns = o.userColumns;
        cachedHash = o.cachedHash;
        hashDirty.store(o.hashDirty.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
        return *this;
    }
    ArchetypeChunk& operator=(ArchetypeChunk&& o) noexcept {
        if (this == &o) return *this;
        mask = o.mask;
        denseToSlot = std::move(o.denseToSlot);
        entities = std::move(o.entities);
        transforms = std::move(o.transforms);
        velocities = std::move(o.velocities);
        renderTags = std::move(o.renderTags);
        userData = std::move(o.userData);
        accelerations = std::move(o.accelerations);
        parents = std::move(o.parents);
        healths = std::move(o.healths);
        factions = std::move(o.factions);
        animationStates = std::move(o.animationStates);
        physicsBodies = std::move(o.physicsBodies);
        navAgents = std::move(o.navAgents);
        boundingVolumes = std::move(o.boundingVolumes);
        masks = std::move(o.masks);
        userColumns = std::move(o.userColumns);
        cachedHash = o.cachedHash;
        hashDirty.store(o.hashDirty.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
        return *this;
    }

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

    /// SHARDED_OPTIMISATION.md S6 — One swap-pop event recorded by
    /// `migrateBatch`. The k-th event corresponds to the k-th pop in
    /// descending-srcRow order. `swappedSlot == UINT32_MAX` means the
    /// pop touched the last row directly (no swap happened); in that
    /// case `newRow` is also UINT32_MAX.
    struct BatchSwapEvent {
        std::uint32_t swappedSlot = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t newRow      = std::numeric_limits<std::uint32_t>::max();
    };

    /// SHARDED_OPTIMISATION.md S6 — Batch-migrate @c srcRows.size()
    /// entities from @p srcArch to the archetype matching @p dstMask.
    /// Equivalent to N independent `migrate(srcArch, srcRows[i], dstMask)`
    /// calls in submission order, but amortizes:
    ///   - one `getOrCreateArchetype(dstMask)` lookup,
    ///   - one `reserveChunkRows(dstMask, N)` capacity hint,
    ///   - one src/dst chunk reference (cached over the whole batch),
    ///   - one user-column carry buffer (reused across iterations).
    ///
    /// Insertion into the destination archetype happens in submission
    /// order — `outDstRows[i]` is the destination row for `srcRows[i]`,
    /// monotonically increasing from `oldDstSize` to `oldDstSize + N - 1`.
    /// Pops from the source archetype happen in *descending* srcRow
    /// order to keep the swap-and-pop semantics consistent with
    /// independent `migrate` calls — see the per-step-by-step trace in
    /// SHARDED_OPTIMISATION.md §7 S6 outcome.
    ///
    /// `outSwaps[k]` records the k-th pop's swap target (k indexes into
    /// the descending-srcRow permutation). The caller updates each
    /// `outSwaps[k].swappedSlot`'s row to `outSwaps[k].newRow`, in any
    /// order — when one slot is swapped multiple times during the batch
    /// the events naturally appear in pop order so the LAST event has
    /// the final row.
    ///
    /// @pre  srcArch < chunks().size()
    /// @pre  every srcRows[i] < chunks()[srcArch].size()
    /// @pre  outDstRows.size() == srcRows.size()
    /// @pre  outSwaps.size()   == srcRows.size()
    /// @returns the destination archetype index.
    ///
    /// @thread_safety Sim-thread only — same constraint as the
    /// per-cmd `migrate`. Determinism: bit-for-bit identical final
    /// chunk contents to N independent `migrate` calls in submission
    /// order (verified by `tests/migration_batch_test.cpp` against
    /// `archetype_hash_determinism_test`'s reference hashes).
    std::uint32_t migrateBatch(std::uint32_t srcArch,
                               ComponentSet  dstMask,
                               std::span<const std::uint32_t> srcRows,
                               std::span<std::uint32_t>       outDstRows,
                               std::span<BatchSwapEvent>      outSwaps);

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

    /// §3.9.4 batch 19 — hint: reserve `extra` additional rows in the
    /// chunk for @p dstMask, creating the chunk if needed. Called by
    /// `commitBuffer` when it detects a run of consecutive mask-toggling
    /// commands whose entities are about to migrate into the same
    /// destination archetype. Amortizes the geometric vector growth
    /// inside the destination chunk's per-component vectors.
    ///
    /// The bytes written by the subsequent migrations are unchanged;
    /// only `vector::capacity()` moves. Commit semantics + commit-hash
    /// are bit-for-bit identical with or without the hint.
    void reserveChunkRows(ComponentSet dstMask, std::size_t extra);

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

    // SHARDED_OPTIMISATION.md S6 — `migrateBatch` reuses this between
    // calls so the steady-state pays zero allocations after the first
    // tick. Sim-thread serial; the commit phase is the sole touch
    // point.
    std::vector<std::uint32_t> batchPermScratch_;
};

} // namespace threadmaxx::internal
