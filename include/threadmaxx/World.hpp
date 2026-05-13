#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace threadmaxx {

namespace internal { class WorldImpl; }

/// Read-only view of the authoritative simulation state.
///
/// Worker jobs receive a `const World&` and may read freely but must not
/// mutate. All mutations flow through @ref CommandBuffer and are applied
/// on the simulation thread during the commit phase. This invariant is
/// what makes the engine deterministic and lock-free for gameplay code.
///
/// Dense iteration: the `entities()`, `transforms()`, `velocities()` etc.
/// spans are parallel arrays — index `i` of every span refers to the
/// same entity. The spans are stable for the duration of a system's
/// `update()` but a spawn or destroy in the commit phase can reorder
/// them, so do not retain pointers across ticks.
class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;

    /// @name Per-entity lookup
    /// Returns `nullptr` if the handle is stale or the entity does not
    /// carry the component (the per-entity component mask is consulted).
    /// @{
    const Transform*    tryGetTransform   (EntityHandle e) const noexcept;
    const Velocity*     tryGetVelocity    (EntityHandle e) const noexcept;
    const RenderTag*    tryGetRenderTag   (EntityHandle e) const noexcept;
    const UserData*     tryGetUserData    (EntityHandle e) const noexcept;
    const Acceleration* tryGetAcceleration(EntityHandle e) const noexcept;
    const Parent*       tryGetParent      (EntityHandle e) const noexcept;
    /// Per-entity component-presence bitset. Returns nullptr for stale
    /// handles.
    const ComponentSet* tryGetComponentMask(EntityHandle e) const noexcept;
    /// @}

    bool alive(EntityHandle e) const noexcept;

    /// @name Dense iteration
    /// Parallel jobs index into these contiguous arrays. The i-th element
    /// of every span corresponds to the i-th live entity.
    /// @{
    std::span<const EntityHandle> entities() const noexcept;
    std::span<const Transform>    transforms() const noexcept;
    std::span<const Velocity>     velocities() const noexcept;
    std::span<const RenderTag>    renderTags() const noexcept;
    std::span<const UserData>     userData()   const noexcept;
    std::span<const Acceleration> accelerations() const noexcept;
    std::span<const Parent>       parents()       const noexcept;
    std::span<const ComponentSet> componentMasks() const noexcept;
    /// @}

    /// Number of live entities.
    std::size_t size() const noexcept;

    /// @internal Engine-internal access; do not call from game code.
    internal::WorldImpl& impl_() noexcept { return *impl_ptr_; }
    /// @internal Engine-internal access; do not call from game code.
    const internal::WorldImpl& impl_() const noexcept { return *impl_ptr_; }

private:
    std::unique_ptr<internal::WorldImpl> impl_ptr_;
};

} // namespace threadmaxx
