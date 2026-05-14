#pragma once

#include "threadmaxx/Components.hpp"
#include "threadmaxx/Handles.hpp"

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace threadmaxx::internal {

// Dense, parallel-array storage for entities. Index N of every component
// array corresponds to the same entity. Generations are tracked in a parallel
// array so we can validate handles without searching.
//
// This type is NOT thread-safe. It is only mutated from the simulation
// thread during the commit phase. During system update phase it is read-only
// and freely shared with workers.
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

    // §3.5 reserveHandle: allocate a slot ahead of materialization.
    //
    // Thread-safe under reservationMtx_: workers can call from inside
    // ISystem::update jobs. The returned handle reads as alive()==false
    // until it is fed to materializeReserved (or discarded). Other
    // EntityStorage methods (spawn/destroy/mut*) must NOT be called
    // concurrently — they assume single-threaded sim-thread access.
    EntityHandle reserveHandle();

    // Batch form: reserve `count` slots under a single acquisition of
    // reservationMtx_. The returned handles are independent (different
    // slot indices, fresh generations) and may be materialized in any
    // order via materializeReserved. Amortizes the per-call mutex cost
    // when a job spawns many entities at once.
    void reserveHandles(std::uint32_t count,
                        std::span<EntityHandle> out);

    // Materialize a previously-reserved slot into a live entity. Returns
    // false if the handle is not a current reservation (e.g. it was
    // already materialized, or referred to a stale generation). Caller
    // is the commit phase, single-threaded.
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

    // Drop every reservation that survived the commit phase. Each
    // dropped reservation has its generation bumped (so the outstanding
    // handle stops validating) and its slot returned to the free list.
    // Single-threaded; called at step end.
    void discardAllReservations() noexcept;

    // Returns true if the handle was valid and the entity was destroyed.
    bool destroy(EntityHandle h) noexcept;

    bool alive(EntityHandle h) const noexcept;

    // Returns the dense index for a handle, or UINT32_MAX if not alive.
    std::uint32_t indexOf(EntityHandle h) const noexcept;

    std::size_t size() const noexcept { return entities_.size(); }

    // Dense views — stable for the duration of a system update phase.
    const std::vector<EntityHandle>&      entities()        const noexcept { return entities_; }
    const std::vector<Transform>&         transforms()      const noexcept { return transforms_; }
    const std::vector<Velocity>&          velocities()      const noexcept { return velocities_; }
    const std::vector<RenderTag>&         renderTags()      const noexcept { return renderTags_; }
    const std::vector<UserData>&          userData()        const noexcept { return userData_; }
    const std::vector<Acceleration>&      accelerations()   const noexcept { return accelerations_; }
    const std::vector<Parent>&            parents()         const noexcept { return parents_; }
    const std::vector<Health>&            healths()         const noexcept { return healths_; }
    const std::vector<Faction>&           factions()        const noexcept { return factions_; }
    const std::vector<AnimationStateRef>& animationStates() const noexcept { return animationStates_; }
    const std::vector<PhysicsBodyRef>&    physicsBodies()   const noexcept { return physicsBodies_; }
    const std::vector<NavAgentRef>&       navAgents()       const noexcept { return navAgents_; }
    const std::vector<BoundingVolume>&    boundingVolumes() const noexcept { return boundingVolumes_; }
    const std::vector<ComponentSet>&      componentMasks()  const noexcept { return masks_; }

    // Mutators — only called from the commit phase.
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
    ComponentSet*      mutComponentMask    (EntityHandle h) noexcept;

    void reserve(std::size_t n);

private:
    // Per-slot record. Slots are reused after destroy(); generation is
    // bumped so old handles stop validating. `reserved` means
    // reserveHandle() handed out a handle for this slot but
    // materializeReserved() has not yet run (alive==false in that state).
    struct Slot {
        std::uint32_t denseIndex = 0;   // index into the parallel arrays
        std::uint32_t generation = 0;   // 0 means "never used"
        bool          alive      = false;
        bool          reserved   = false;  // see §3.5
    };

    std::vector<Slot>         slots_;        // sparse, indexed by handle.index
    std::vector<std::uint32_t> freeSlots_;   // recycled slot indices

    // §3.5 reservation tracking. Touched by reserveHandle (workers) and
    // by materializeReserved/discardAllReservations (sim thread); the
    // mutex covers all three so worker pushes don't race with sim-thread
    // drains.
    std::mutex                reservationMtx_;
    std::vector<EntityHandle> reservedHandles_;

    // Parallel dense arrays. denseToSlot_[i] gives the slot index that owns
    // dense row i — used when we swap-and-pop during destroy().
    std::vector<std::uint32_t>      denseToSlot_;
    std::vector<EntityHandle>       entities_;
    std::vector<Transform>          transforms_;
    std::vector<Velocity>           velocities_;
    std::vector<RenderTag>          renderTags_;
    std::vector<UserData>           userData_;
    std::vector<Acceleration>       accelerations_;
    std::vector<Parent>             parents_;
    std::vector<Health>             healths_;
    std::vector<Faction>            factions_;
    std::vector<AnimationStateRef>  animationStates_;
    std::vector<PhysicsBodyRef>     physicsBodies_;
    std::vector<NavAgentRef>        navAgents_;
    std::vector<BoundingVolume>     boundingVolumes_;
    std::vector<ComponentSet>       masks_;
};

} // namespace threadmaxx::internal
