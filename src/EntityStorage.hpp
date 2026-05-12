#pragma once

#include "threadmaxx/Components.hpp"
#include "threadmaxx/Handles.hpp"

#include <cstdint>
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
                       const UserData& u);

    // Returns true if the handle was valid and the entity was destroyed.
    bool destroy(EntityHandle h) noexcept;

    bool alive(EntityHandle h) const noexcept;

    // Returns the dense index for a handle, or UINT32_MAX if not alive.
    std::uint32_t indexOf(EntityHandle h) const noexcept;

    std::size_t size() const noexcept { return entities_.size(); }

    // Dense views — stable for the duration of a system update phase.
    const std::vector<EntityHandle>& entities()   const noexcept { return entities_; }
    const std::vector<Transform>&    transforms() const noexcept { return transforms_; }
    const std::vector<Velocity>&     velocities() const noexcept { return velocities_; }
    const std::vector<RenderTag>&    renderTags() const noexcept { return renderTags_; }
    const std::vector<UserData>&     userData()   const noexcept { return userData_; }

    // Mutators — only called from the commit phase.
    Transform* mutTransform(EntityHandle h) noexcept;
    Velocity*  mutVelocity (EntityHandle h) noexcept;
    RenderTag* mutRenderTag(EntityHandle h) noexcept;
    UserData*  mutUserData (EntityHandle h) noexcept;

    void reserve(std::size_t n);

private:
    // Per-slot record. Slots are reused after destroy(); generation is
    // bumped so old handles stop validating.
    struct Slot {
        std::uint32_t denseIndex = 0;   // index into the parallel arrays
        std::uint32_t generation = 0;   // 0 means "never used"
        bool          alive      = false;
    };

    std::vector<Slot>         slots_;        // sparse, indexed by handle.index
    std::vector<std::uint32_t> freeSlots_;   // recycled slot indices

    // Parallel dense arrays. denseToSlot_[i] gives the slot index that owns
    // dense row i — used when we swap-and-pop during destroy().
    std::vector<std::uint32_t> denseToSlot_;
    std::vector<EntityHandle>  entities_;
    std::vector<Transform>     transforms_;
    std::vector<Velocity>      velocities_;
    std::vector<RenderTag>     renderTags_;
    std::vector<UserData>      userData_;
};

} // namespace threadmaxx::internal
