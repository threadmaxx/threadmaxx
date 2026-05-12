#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace threadmaxx {

namespace internal { class WorldImpl; }

// Read-only view of the authoritative simulation state. Worker jobs receive a
// const reference to a World; they may read freely but must not mutate. All
// mutations flow through CommandBuffer and are applied on the simulation
// thread during the commit phase.
class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;

    // Lookup. Returns nullptr if the handle is stale or not present.
    const Transform*    tryGetTransform   (EntityHandle e) const noexcept;
    const Velocity*     tryGetVelocity    (EntityHandle e) const noexcept;
    const RenderTag*    tryGetRenderTag   (EntityHandle e) const noexcept;
    const UserData*     tryGetUserData    (EntityHandle e) const noexcept;
    const Acceleration* tryGetAcceleration(EntityHandle e) const noexcept;

    bool alive(EntityHandle e) const noexcept;

    // Dense iteration: parallel jobs index into these contiguous arrays. The
    // i-th element of every span corresponds to the i-th live entity.
    std::span<const EntityHandle> entities() const noexcept;
    std::span<const Transform>    transforms() const noexcept;
    std::span<const Velocity>     velocities() const noexcept;
    std::span<const RenderTag>    renderTags() const noexcept;
    std::span<const UserData>     userData()   const noexcept;
    std::span<const Acceleration> accelerations() const noexcept;

    std::size_t size() const noexcept;

    // Engine-internal access; do not call from game code.
    internal::WorldImpl& impl_() noexcept { return *impl_ptr_; }
    const internal::WorldImpl& impl_() const noexcept { return *impl_ptr_; }

private:
    std::unique_ptr<internal::WorldImpl> impl_ptr_;
};

} // namespace threadmaxx
