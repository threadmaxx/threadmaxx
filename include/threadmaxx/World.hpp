#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace threadmaxx { struct WorldSnapshot; }

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

    /// Header-only sugar: true iff @p e is alive AND the entity's
    /// per-entity component-presence mask has the bit for component `T`.
    ///
    /// `T` must be one of the built-in component types: Transform,
    /// Velocity, RenderTag, UserData, Acceleration, Parent. Cheaper than
    /// `tryGet<T>() != nullptr` because it consults the mask instead of
    /// returning a pointer the caller is expected to discard.
    template <typename T>
    bool has(EntityHandle e) const noexcept;

    /// Header-only sugar: returns a const reference to the entity's
    /// component `T`. Asserts that the handle is alive and the mask has
    /// the bit; use @ref has or @ref tryGetTransform et al. when absence
    /// is a legal state.
    template <typename T>
    const T& get(EntityHandle e) const noexcept;

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

    /// Capture the world's dense arrays into a @ref WorldSnapshot. The
    /// snapshot is a stable copy — safe to keep across ticks, safe to
    /// serialize via @ref serialize from `<threadmaxx/Serialization.hpp>`.
    ///
    /// Called from gameplay code; not thread-safe against concurrent
    /// commits (run it from `postStep` or after `engine.step()` returns,
    /// not from inside a wave system's `update`).
    WorldSnapshot snapshot() const;

    /// @internal Engine-internal access; do not call from game code.
    internal::WorldImpl& impl_() noexcept { return *impl_ptr_; }
    /// @internal Engine-internal access; do not call from game code.
    const internal::WorldImpl& impl_() const noexcept { return *impl_ptr_; }

private:
    std::unique_ptr<internal::WorldImpl> impl_ptr_;
};

namespace detail {

template <typename T>
constexpr Component worldComponentBit() noexcept {
    if constexpr (std::is_same_v<T, Transform>)         return Component::Transform;
    else if constexpr (std::is_same_v<T, Velocity>)     return Component::Velocity;
    else if constexpr (std::is_same_v<T, RenderTag>)    return Component::RenderTag;
    else if constexpr (std::is_same_v<T, UserData>)     return Component::UserData;
    else if constexpr (std::is_same_v<T, Acceleration>) return Component::Acceleration;
    else if constexpr (std::is_same_v<T, Parent>)       return Component::Parent;
    else static_assert(sizeof(T) == 0,
        "World::has/get: T must be one of Transform, Velocity, RenderTag, "
        "UserData, Acceleration, Parent");
}

template <typename T>
const T* worldTryGetSpanElement(const World& w, EntityHandle e) noexcept {
    if constexpr (std::is_same_v<T, Transform>)         return w.tryGetTransform(e);
    else if constexpr (std::is_same_v<T, Velocity>)     return w.tryGetVelocity(e);
    else if constexpr (std::is_same_v<T, RenderTag>)    return w.tryGetRenderTag(e);
    else if constexpr (std::is_same_v<T, UserData>)     return w.tryGetUserData(e);
    else if constexpr (std::is_same_v<T, Acceleration>) return w.tryGetAcceleration(e);
    else if constexpr (std::is_same_v<T, Parent>)       return w.tryGetParent(e);
    else static_assert(sizeof(T) == 0,
        "World::get: T must be one of Transform, Velocity, RenderTag, "
        "UserData, Acceleration, Parent");
}

} // namespace detail

template <typename T>
inline bool World::has(EntityHandle e) const noexcept {
    const auto* mask = tryGetComponentMask(e);
    return mask != nullptr && mask->has(detail::worldComponentBit<T>());
}

template <typename T>
inline const T& World::get(EntityHandle e) const noexcept {
    assert(has<T>(e) && "World::get<T>: entity is not alive or does not carry T");
    return *detail::worldTryGetSpanElement<T>(*this, e);
}

} // namespace threadmaxx
