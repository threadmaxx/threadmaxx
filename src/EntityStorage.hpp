#pragma once

#include "threadmaxx/internal/Archetype.hpp"
#include "threadmaxx/Components.hpp"
#include "threadmaxx/Handles.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>
#include <vector>

namespace threadmaxx::internal {

/// Archetype-keyed storage for entities (§3.1 batch 6).
///
/// Entities live in @ref ArchetypeChunk instances keyed by their per-
/// entity @ref ComponentSet. Per-entity slots (sparse, indexed by
/// @ref EntityHandle::index) record which chunk and row each live
/// entity occupies; chunks own the dense component arrays.
///
/// The legacy "one flat dense array per component" view is reconstructed
/// on demand via the lazy stitched cache: a public dense accessor like
/// @ref transforms walks the chunks in creation order and concatenates
/// each chunk's per-row values into a single contiguous vector, filling
/// default values for entities whose chunk doesn't carry the component.
/// The cache is invalidated by any mutation and rebuilt the next time
/// a dense accessor is called.
///
/// This type is NOT thread-safe. It is only mutated from the simulation
/// thread during the commit phase. During system update phase it is
/// read-only and freely shared with workers.
class EntityStorage {
public:
    explicit EntityStorage(std::uint32_t initialCapacity);

    EntityHandle spawn(const Transform& t,
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
                       ComponentSet initialMask);

    EntityHandle reserveHandle();

    void reserveHandles(std::uint32_t count,
                        std::span<EntityHandle> out);

    bool materializeReserved(EntityHandle h,
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
                             ComponentSet initialMask);

    void discardAllReservations() noexcept;

    bool destroy(EntityHandle h) noexcept;

    bool alive(EntityHandle h) const noexcept;

    /// Stitched-view dense index for the entity, or `UINT32_MAX` if not
    /// alive. The returned index is valid into the stitched dense
    /// vectors returned by @ref entities / @ref transforms / etc.,
    /// and changes whenever the chunk layout changes (spawn / destroy
    /// / migrate). Engine internal — game code should use
    /// `World::tryGet*` / `World::has<T>` instead.
    std::uint32_t indexOf(EntityHandle h) const noexcept;

    std::size_t size() const noexcept { return table_.totalEntities(); }

    /// @name Stitched dense views
    /// Rebuilt on first read after any mutation. Index @c i of every
    /// span refers to the same entity; for entities whose chunk does
    /// not carry a particular component, the corresponding span entry
    /// is the component's default-constructed value.
    /// @{
    std::span<const EntityHandle>      entities()        const noexcept;
    std::span<const Transform>         transforms()      const noexcept;
    std::span<const Velocity>          velocities()      const noexcept;
    std::span<const RenderTag>         renderTags()      const noexcept;
    std::span<const UserData>          userData()        const noexcept;
    std::span<const Acceleration>      accelerations()   const noexcept;
    std::span<const Parent>            parents()         const noexcept;
    std::span<const Health>            healths()         const noexcept;
    std::span<const Faction>           factions()        const noexcept;
    std::span<const AnimationStateRef> animationStates() const noexcept;
    std::span<const PhysicsBodyRef>    physicsBodies()   const noexcept;
    std::span<const NavAgentRef>       navAgents()       const noexcept;
    std::span<const BoundingVolume>    boundingVolumes() const noexcept;
    std::span<const ComponentSet>      componentMasks()  const noexcept;
    /// @}

    /// @name Per-handle mutators (commit-phase only)
    /// Each returns a pointer to the entity's chunk-resident value, or
    /// nullptr when the chunk's mask does not carry the component (the
    /// archetype refactor makes "no slot" the source of truth for
    /// absence). Callers wanting to attach an absent component must
    /// first migrate the entity via @ref setMaskAndMigrate.
    /// @{
    Transform*         mutTransform        (EntityHandle h) noexcept;
    Velocity*          mutVelocity         (EntityHandle h) noexcept;
    RenderTag*         mutRenderTag        (EntityHandle h) noexcept;
    UserData*          mutUserData         (EntityHandle h) noexcept;
    Acceleration*      mutAcceleration     (EntityHandle h) noexcept;
    Parent*            mutParent           (EntityHandle h) noexcept;
    Health*            mutHealth           (EntityHandle h) noexcept;
    Faction*           mutFaction          (EntityHandle h) noexcept;
    AnimationStateRef* mutAnimationStateRef(EntityHandle h) noexcept;
    PhysicsBodyRef*    mutPhysicsBodyRef   (EntityHandle h) noexcept;
    NavAgentRef*       mutNavAgentRef      (EntityHandle h) noexcept;
    BoundingVolume*    mutBoundingVolume   (EntityHandle h) noexcept;
    /// @}

    /// Per-handle component-mask accessor. Returns the chunk's
    /// archetype mask (every entity in a chunk shares the chunk's
    /// mask).
    const ComponentSet* tryGetComponentMask(EntityHandle h) const noexcept;

    /// Commit-phase only. Migrates the entity to the archetype whose
    /// mask is @p newMask, preserving every component value that exists
    /// in both the source and destination archetypes. Dropped
    /// components lose their dense slot; added components land in
    /// default-initialized slots (callers should follow up with the
    /// corresponding @c mut* setter where they hold a fresh value).
    bool setMaskAndMigrate(EntityHandle h, ComponentSet newMask) noexcept;

    /// Lookup helper used by hot paths (chunk iteration, debug tools):
    /// returns (archetype index, row) for a handle, or `(UINT32_MAX,
    /// UINT32_MAX)` if not alive. The archetype/row encoding is the
    /// post-refactor replacement for the legacy single dense index.
    struct Location { std::uint32_t archetype; std::uint32_t row; };
    Location locate(EntityHandle h) const noexcept;

    /// Archetype table access for chunk iteration (@ref forEachChunk).
    const ArchetypeTable& archetypes() const noexcept { return table_; }
    ArchetypeTable&       archetypes()       noexcept { return table_; }

    void reserve(std::size_t n);

private:
    /// Per-slot record. The post-refactor location is (archetype, row);
    /// `generation` and `alive` keep their pre-refactor semantics so
    /// handle validation continues to work unchanged.
    struct Slot {
        std::uint32_t archetypeIndex = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t row            = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t generation     = 0;
        bool          alive          = false;
        bool          reserved       = false;
    };

    std::vector<Slot>          slots_;
    std::vector<std::uint32_t> freeSlots_;

    std::mutex                reservationMtx_;
    std::vector<EntityHandle> reservedHandles_;

    ArchetypeTable            table_;

    // Stitched (legacy linear) view of every dense component. Rebuilt
    // lazily — every mutation marks the cache dirty; the next public
    // accessor walks the archetype chunks and rebuilds. The cache is
    // mutable so const accessors can refresh it.
    //
    // §3.6 batch 13b — atomic so worker threads applying chunk-local
    // value-only commands (the sharded commit path) can flip the flag
    // without a data race. The store is `relaxed`: ordering across
    // chunks doesn't matter, only that the flag is observed as `true`
    // before the next `ensureStitched()` runs.
    //
    // §3.6 batch 14 fix — the cache-rebuild step itself touches ~14
    // non-atomic vectors. If two worker jobs in a wave both call a
    // legacy dense accessor (`world.transforms()` et al.) on a dirty
    // cache, both enter `ensureStitched` and race on the writes.
    // `stitchedMtx_` serializes the rebuild; the double-checked
    // pattern in `ensureStitched()` keeps the steady-state hit
    // (cache clean) lock-free via the atomic flag.
    mutable std::atomic<bool>               stitchedDirty_{true};
    mutable std::mutex                      stitchedMtx_;
    mutable std::vector<EntityHandle>       stitchedEntities_;
    mutable std::vector<Transform>          stitchedTransforms_;
    mutable std::vector<Velocity>           stitchedVelocities_;
    mutable std::vector<RenderTag>          stitchedRenderTags_;
    mutable std::vector<UserData>           stitchedUserData_;
    mutable std::vector<Acceleration>       stitchedAccelerations_;
    mutable std::vector<Parent>             stitchedParents_;
    mutable std::vector<Health>             stitchedHealths_;
    mutable std::vector<Faction>            stitchedFactions_;
    mutable std::vector<AnimationStateRef>  stitchedAnimationStates_;
    mutable std::vector<PhysicsBodyRef>     stitchedPhysicsBodies_;
    mutable std::vector<NavAgentRef>        stitchedNavAgents_;
    mutable std::vector<BoundingVolume>     stitchedBoundingVolumes_;
    mutable std::vector<ComponentSet>       stitchedMasks_;
    // archetype index → start offset into the stitched arrays. Used by
    // indexOf to translate a slot's (arch, row) into a stitched index.
    mutable std::vector<std::uint32_t>      archetypeStitchStart_;

    void markDirty() noexcept { stitchedDirty_.store(true, std::memory_order_relaxed); }
    void ensureStitched() const noexcept;
};

} // namespace threadmaxx::internal
