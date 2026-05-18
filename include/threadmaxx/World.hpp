#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace threadmaxx { struct WorldSnapshot; }

namespace threadmaxx::internal {
struct ArchetypeChunk;

/// §3.10.3 batch 24 (F13) — `ArchetypeChunk` is forward-declared in
/// `World.hpp` to keep the header light; these free helpers let the
/// `World::forEachChunkOf` template predicate check chunk-vs-mask
/// AND skip empty chunks without consumers having to include
/// `internal/Archetype.hpp`. Defined in `World.cpp`.
bool chunkMaskHasAll(const ArchetypeChunk& chunk, ComponentSet required) noexcept;
bool chunkIsEmpty   (const ArchetypeChunk& chunk) noexcept;
} // namespace threadmaxx::internal

namespace threadmaxx {

/// One row of @ref World::archetypeSignatures. Each row describes a
/// distinct per-entity @ref ComponentSet currently in use and how many
/// live entities carry exactly that mask.
///
/// Each row corresponds to a physical archetype chunk group (§3.1
/// batch 6); the signature is the chunk's mask and the count is the
/// chunk's row count.
struct ArchetypeSignature {
    ComponentSet  mask  = {};
    std::uint32_t count = 0;
};

namespace internal { class WorldImpl; }

/// Result of @ref World::locate. Pinpoints an entity's
/// (archetype-chunk-index, row) in the storage. `archetype ==
/// UINT32_MAX` indicates a stale handle.
struct ArchetypeLocation {
    std::uint32_t archetype = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t row       = std::numeric_limits<std::uint32_t>::max();

    bool valid() const noexcept {
        return archetype != std::numeric_limits<std::uint32_t>::max();
    }
};

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
    const Transform*         tryGetTransform        (EntityHandle e) const noexcept;
    const Velocity*          tryGetVelocity         (EntityHandle e) const noexcept;
    const RenderTag*         tryGetRenderTag        (EntityHandle e) const noexcept;
    const UserData*          tryGetUserData         (EntityHandle e) const noexcept;
    const Acceleration*      tryGetAcceleration     (EntityHandle e) const noexcept;
    const Parent*            tryGetParent           (EntityHandle e) const noexcept;
    const Health*            tryGetHealth           (EntityHandle e) const noexcept;
    const Faction*           tryGetFaction          (EntityHandle e) const noexcept;
    const AnimationStateRef* tryGetAnimationStateRef(EntityHandle e) const noexcept;
    const PhysicsBodyRef*    tryGetPhysicsBodyRef   (EntityHandle e) const noexcept;
    const NavAgentRef*       tryGetNavAgentRef      (EntityHandle e) const noexcept;
    const BoundingVolume*    tryGetBoundingVolume   (EntityHandle e) const noexcept;
    /// Per-entity component-presence bitset. Returns nullptr for stale
    /// handles.
    const ComponentSet*      tryGetComponentMask    (EntityHandle e) const noexcept;
    /// @}

    bool alive(EntityHandle e) const noexcept;

    /// Header-only sugar: true iff @p e is alive AND the entity's
    /// per-entity component-presence mask has the bit for component `T`.
    ///
    /// `T` must be one of the built-in component types: Transform,
    /// Velocity, RenderTag, UserData, Acceleration, Parent, Health,
    /// Faction, AnimationStateRef, PhysicsBodyRef, NavAgentRef,
    /// BoundingVolume — or one of the tag-only categories
    /// (`StaticTag`, `DisabledTag`, `DestroyedTag`), each addressed by
    /// passing the corresponding @ref Component enum as a non-type
    /// template parameter via @ref hasTag.
    template <typename T>
    bool has(EntityHandle e) const noexcept;

    /// Tag-only presence check. Use for `Component::StaticTag` /
    /// `DisabledTag` / `DestroyedTag` — tags have no dense storage, so
    /// the bit alone defines presence. Returns false if the handle is
    /// stale.
    bool hasTag(EntityHandle e, Component tag) const noexcept;

    /// Header-only sugar: returns a const reference to the entity's
    /// component `T`. Asserts that the handle is alive and the mask has
    /// the bit; use @ref has or @ref tryGetTransform et al. when absence
    /// is a legal state.
    template <typename T>
    const T& get(EntityHandle e) const noexcept;

    /// @name Dense iteration
    /// Parallel jobs index into these contiguous arrays. The i-th element
    /// of every span corresponds to the i-th live entity.
    ///
    /// @par Stitched-view contract (§3.10.2 batch 22 — F4 doc)
    ///      Internally these spans are backed by a lazy *stitched
    ///      cache* that concatenates per-chunk vectors. The cache is
    ///      mutex-guarded for parallel readers (batch-15a fix), but
    ///      the contract for **the sharded commit phase**
    ///      (`Config::singleThreadedCommit = false`) is subtle:
    ///      Pass-C worker jobs invoke `mut*()` to write into chunk
    ///      vectors, which flips `stitchedDirty_` while the writers
    ///      are still in flight. A reader on another thread that
    ///      polls e.g. `world.transforms()` *during* a sharded
    ///      Pass C will see either the pre-commit snapshot or
    ///      trigger a rebuild that reads chunks the workers are
    ///      concurrently writing — undefined behavior.
    ///
    ///      The safe consumption pattern is:
    ///        - **Inside a wave** (`update()` / `parallelFor` body):
    ///          read the stitched view freely from worker jobs that
    ///          do not also write through `mut*()`. The chunked
    ///          path (`forEachChunk` / `worldView()`) is preferred
    ///          and pays no stitched-view cost.
    ///        - **From a non-sim thread between waves**: only when
    ///          `Config::singleThreadedCommit = true` (the default),
    ///          or when no sharded commit is in flight.
    ///        - **For game-code introspection**: the
    ///          `World::has<T>` / `World::get<T>` and the
    ///          `forEachChunk<T...>` paths are mutex-free and
    ///          always safe.
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

    /// Inventory of distinct per-entity @ref ComponentSet values currently
    /// live in the world, with per-mask counts.
    ///
    /// O(num archetypes) — one row per non-empty @ref
    /// internal::ArchetypeChunk, sorted by `mask.bits()` ascending so
    /// output order is stable across runs. Useful for HUD/profiling
    /// ("how many distinct archetypes does my world have?") and for
    /// driving custom chunk iteration without going through
    /// @ref forEachChunk.
    ///
    /// Not thread-safe against concurrent commits; same caveat as
    /// @ref snapshot.
    std::vector<ArchetypeSignature> archetypeSignatures() const;

    /// @name Archetype chunk iteration (§3.1 batch 6)
    ///
    /// Number of @ref internal::ArchetypeChunk groups currently in the
    /// world, and read-only access to each by index. Game code should
    /// use the higher-level @ref forEachChunk helper in `Query.hpp`;
    /// these accessors are the foundation it builds on, but they're
    /// public so users who need a custom traversal strategy don't have
    /// to reach through `impl_()`.
    /// @{
    std::size_t archetypeChunkCount() const noexcept;
    const internal::ArchetypeChunk& archetypeChunk(std::size_t i) const noexcept;

    /// §3.10.3 batch 24 (F13) — introspection helper for iterating
    /// chunks whose mask satisfies a required set. Saves callers from
    /// the `for (i = 0..archetypeChunkCount()) { if (mask.hasAll(req))
    /// ... }` boilerplate. Single-threaded by design — this is the
    /// introspection / debug-tooling path, not a parallel-dispatch
    /// path. Use @ref forEachChunk in `Query.hpp` for parallel work.
    ///
    /// @param required  Mask the chunk must satisfy (`chunk.mask.hasAll(required)`).
    ///                  Pass `ComponentSet::none()` to visit every non-empty chunk.
    /// @param fn        Invocable as `fn(const internal::ArchetypeChunk&)`.
    ///                  Visit order matches `archetypeChunk(i)` for `i =
    ///                  0..archetypeChunkCount()-1` (stable across runs).
    ///                  Empty chunks (zero entities) are skipped — same
    ///                  policy as `forEachChunk<Required...>` in
    ///                  `Query.hpp`. The engine retains a default
    ///                  empty `all()`-masked chunk at construction;
    ///                  callers should not have to filter it.
    template <typename F>
    void forEachChunkOf(ComponentSet required, F&& fn) const {
        const std::size_t count = archetypeChunkCount();
        for (std::size_t i = 0; i < count; ++i) {
            const auto& chunk = archetypeChunk(i);
            if (internal::chunkIsEmpty(chunk)) continue;
            if (internal::chunkMaskHasAll(chunk, required)) {
                fn(chunk);
            }
        }
    }
    /// @}

    /// Locate an entity in chunked storage. Returns `(UINT32_MAX,
    /// UINT32_MAX)` for stale handles. Used by per-handle accessors and
    /// the §3.1 batch-6b user-component helpers (@ref user::tryGet) to
    /// jump straight to the entity's chunk row without touching the
    /// stitched view.
    ArchetypeLocation locate(EntityHandle e) const noexcept;

    /// @internal Engine-internal access; do not call from game code.
    internal::WorldImpl& impl_() noexcept { return *impl_ptr_; }
    /// @internal Engine-internal access; do not call from game code.
    const internal::WorldImpl& impl_() const noexcept { return *impl_ptr_; }

private:
    std::unique_ptr<internal::WorldImpl> impl_ptr_;
};

/// §3.6 batch 13c — wave-scoped read-only view of world chunk storage.
///
/// Within a wave the engine never commits, so the world's chunk count,
/// chunk pointers, and per-chunk row counts are immutable. `WorldView`
/// caches that immutability into a flat array of chunk pointers so
/// systems doing multiple `parallelFor` / `single` passes (or workers
/// passing the chunk list by value into captures) don't pay repeated
/// indirection through @ref World::archetypeChunk.
///
/// The view is rebuilt by the engine before each wave runs and shared
/// across all systems in the wave (they all see the same pre-wave
/// state). Access it via @ref SystemContext::worldView. Outside of
/// `update()` the view is empty; reading it from `preStep` / `postStep`
/// / `buildRenderFrame` is undefined.
///
/// @par Thread-safety
///      Read-only after construction. Safe to capture by reference or
///      copy into worker job lambdas — the underlying chunk pointers
///      remain valid for the wave's duration.
class WorldView {
public:
    WorldView() = default;

    /// Rebuild from `w`'s current chunk inventory. Cheap: O(num
    /// archetypes); chunks themselves are not copied.
    void rebuild(const World& w);

    /// The view's source world. Returns `nullptr` for a default-
    /// constructed view that was never built against a world.
    const World* world() const noexcept { return world_; }

    /// Number of distinct chunks in the world at view-construction
    /// time. Equal to @ref World::archetypeChunkCount.
    std::size_t chunkCount() const noexcept { return chunks_.size(); }

    /// Flat span of pointers to every chunk in the world. Stable for
    /// the wave; cheap to capture in a lambda by value (the span itself
    /// is two words).
    std::span<const internal::ArchetypeChunk* const> chunks() const noexcept {
        return std::span<const internal::ArchetypeChunk* const>(
            chunks_.data(), chunks_.size());
    }

    /// Total number of live entities, summed across all chunks. Cached
    /// at view build time; equivalent to @ref World::size.
    std::size_t entityCount() const noexcept { return entityCount_; }

private:
    const World*                                  world_       = nullptr;
    std::vector<const internal::ArchetypeChunk*>  chunks_;
    std::size_t                                   entityCount_ = 0;
};

namespace detail {

template <typename T>
constexpr Component worldComponentBit() noexcept {
    if constexpr (std::is_same_v<T, Transform>)              return Component::Transform;
    else if constexpr (std::is_same_v<T, Velocity>)          return Component::Velocity;
    else if constexpr (std::is_same_v<T, RenderTag>)         return Component::RenderTag;
    else if constexpr (std::is_same_v<T, UserData>)          return Component::UserData;
    else if constexpr (std::is_same_v<T, Acceleration>)      return Component::Acceleration;
    else if constexpr (std::is_same_v<T, Parent>)            return Component::Parent;
    else if constexpr (std::is_same_v<T, Health>)            return Component::Health;
    else if constexpr (std::is_same_v<T, Faction>)           return Component::Faction;
    else if constexpr (std::is_same_v<T, AnimationStateRef>) return Component::AnimationStateRef;
    else if constexpr (std::is_same_v<T, PhysicsBodyRef>)    return Component::PhysicsBodyRef;
    else if constexpr (std::is_same_v<T, NavAgentRef>)       return Component::NavAgentRef;
    else if constexpr (std::is_same_v<T, BoundingVolume>)    return Component::BoundingVolume;
    else static_assert(sizeof(T) == 0,
        "World::has/get: T must be one of the built-in component types "
        "(Transform, Velocity, RenderTag, UserData, Acceleration, "
        "Parent, Health, Faction, AnimationStateRef, PhysicsBodyRef, "
        "NavAgentRef, BoundingVolume). Tag-only categories go through "
        "World::hasTag.");
}

template <typename T>
const T* worldTryGetSpanElement(const World& w, EntityHandle e) noexcept {
    if constexpr (std::is_same_v<T, Transform>)              return w.tryGetTransform(e);
    else if constexpr (std::is_same_v<T, Velocity>)          return w.tryGetVelocity(e);
    else if constexpr (std::is_same_v<T, RenderTag>)         return w.tryGetRenderTag(e);
    else if constexpr (std::is_same_v<T, UserData>)          return w.tryGetUserData(e);
    else if constexpr (std::is_same_v<T, Acceleration>)      return w.tryGetAcceleration(e);
    else if constexpr (std::is_same_v<T, Parent>)            return w.tryGetParent(e);
    else if constexpr (std::is_same_v<T, Health>)            return w.tryGetHealth(e);
    else if constexpr (std::is_same_v<T, Faction>)           return w.tryGetFaction(e);
    else if constexpr (std::is_same_v<T, AnimationStateRef>) return w.tryGetAnimationStateRef(e);
    else if constexpr (std::is_same_v<T, PhysicsBodyRef>)    return w.tryGetPhysicsBodyRef(e);
    else if constexpr (std::is_same_v<T, NavAgentRef>)       return w.tryGetNavAgentRef(e);
    else if constexpr (std::is_same_v<T, BoundingVolume>)    return w.tryGetBoundingVolume(e);
    else static_assert(sizeof(T) == 0,
        "World::get: T must be one of the built-in component types.");
}

} // namespace detail

template <typename T>
inline bool World::has(EntityHandle e) const noexcept {
    const auto* mask = tryGetComponentMask(e);
    return mask != nullptr && mask->has(detail::worldComponentBit<T>());
}

inline bool World::hasTag(EntityHandle e, Component tag) const noexcept {
    const auto* mask = tryGetComponentMask(e);
    return mask != nullptr && mask->has(tag);
}

template <typename T>
inline const T& World::get(EntityHandle e) const noexcept {
    assert(has<T>(e) && "World::get<T>: entity is not alive or does not carry T");
    return *detail::worldTryGetSpanElement<T>(*this, e);
}

} // namespace threadmaxx
