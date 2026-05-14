#pragma once

#include "CommandBuffer.hpp"
#include "Components.hpp"
#include "Handles.hpp"
#include "System.hpp"
#include "World.hpp"
#include "internal/Archetype.hpp"

#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace threadmaxx {

/// @file Query.hpp
/// Helpers that turn the "grab dense spans, write parallelFor by hand"
/// pattern into a one-liner. Every live entity has every built-in
/// component (parallel dense arrays), so a query over a subset is
/// really "iterate all entities, hand me these refs".
///
/// Example:
/// @code
/// forEach<Transform, Velocity>(ctx,
///     [dt](EntityHandle e, const Transform& t, const Velocity& v,
///          CommandBuffer& cb) {
///         Transform next = t;
///         next.position = t.position + v.linear * dt;
///         cb.setTransform(e, next);
///     });
/// @endcode

namespace detail {

template <typename C>
auto getSpan(const World& w) noexcept {
    if constexpr (std::is_same_v<C, Transform>)              return w.transforms();
    else if constexpr (std::is_same_v<C, Velocity>)          return w.velocities();
    else if constexpr (std::is_same_v<C, RenderTag>)         return w.renderTags();
    else if constexpr (std::is_same_v<C, UserData>)          return w.userData();
    else if constexpr (std::is_same_v<C, Acceleration>)      return w.accelerations();
    else if constexpr (std::is_same_v<C, Parent>)            return w.parents();
    else if constexpr (std::is_same_v<C, Health>)            return w.healths();
    else if constexpr (std::is_same_v<C, Faction>)           return w.factions();
    else if constexpr (std::is_same_v<C, AnimationStateRef>) return w.animationStates();
    else if constexpr (std::is_same_v<C, PhysicsBodyRef>)    return w.physicsBodies();
    else if constexpr (std::is_same_v<C, NavAgentRef>)       return w.navAgents();
    else if constexpr (std::is_same_v<C, BoundingVolume>)    return w.boundingVolumes();
    else static_assert(sizeof(C) == 0,
        "forEach: component type must be one of Transform, Velocity, "
        "RenderTag, UserData, Acceleration, Parent, Health, Faction, "
        "AnimationStateRef, PhysicsBodyRef, NavAgentRef, BoundingVolume");
}

template <typename C>
constexpr Component componentBit() noexcept {
    if constexpr (std::is_same_v<C, Transform>)              return Component::Transform;
    else if constexpr (std::is_same_v<C, Velocity>)          return Component::Velocity;
    else if constexpr (std::is_same_v<C, RenderTag>)         return Component::RenderTag;
    else if constexpr (std::is_same_v<C, UserData>)          return Component::UserData;
    else if constexpr (std::is_same_v<C, Acceleration>)      return Component::Acceleration;
    else if constexpr (std::is_same_v<C, Parent>)            return Component::Parent;
    else if constexpr (std::is_same_v<C, Health>)            return Component::Health;
    else if constexpr (std::is_same_v<C, Faction>)           return Component::Faction;
    else if constexpr (std::is_same_v<C, AnimationStateRef>) return Component::AnimationStateRef;
    else if constexpr (std::is_same_v<C, PhysicsBodyRef>)    return Component::PhysicsBodyRef;
    else if constexpr (std::is_same_v<C, NavAgentRef>)       return Component::NavAgentRef;
    else if constexpr (std::is_same_v<C, BoundingVolume>)    return Component::BoundingVolume;
    else static_assert(sizeof(C) == 0,
        "forEachWith: component type must be one of Transform, Velocity, "
        "RenderTag, UserData, Acceleration, Parent, Health, Faction, "
        "AnimationStateRef, PhysicsBodyRef, NavAgentRef, BoundingVolume");
}

template <typename... Cs>
constexpr ComponentSet requiredMask() noexcept {
    ComponentSet s;
    ((s |= ComponentSet{componentBit<Cs>()}), ...);
    return s;
}

template <typename F, typename Spans, std::size_t... Is>
void invokeAt(F& fn, EntityHandle e, const Spans& spans,
              CommandBuffer& cb, std::uint32_t i,
              std::index_sequence<Is...>) {
    fn(e, std::get<Is>(spans)[i]..., cb);
}

} // namespace detail

/// Parallel iteration over all live entities.
///
/// The callable is invoked as
/// `fn(EntityHandle, const C0&, const C1&, ..., CommandBuffer&)` with
/// one CommandBuffer per worker chunk; the engine commits the buffers
/// in submission order after the call returns.
///
/// @tparam Components Built-in component types to project as dense
///                    references. Today: Transform, Velocity, RenderTag,
///                    UserData.
/// @param ctx   System context (provides world, dt, tick, and the worker
///              pool).
/// @param fn    Per-entity callable.
/// @param grain Job size hint; 0 lets the engine pick.
template <typename... Components, typename F>
void forEach(SystemContext& ctx, F&& fn, std::uint32_t grain = 0) {
    const auto& world = ctx.world();
    const auto entities = world.entities();
    const auto count = static_cast<std::uint32_t>(entities.size());
    if (count == 0) return;

    auto spans = std::make_tuple(detail::getSpan<Components>(world)...);

    ctx.parallelFor(count, grain,
        [entities, spans, fn = std::forward<F>(fn)]
        (Range r, CommandBuffer& cb) mutable {
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                detail::invokeAt(fn, entities[i], spans, cb, i,
                                 std::index_sequence_for<Components...>{});
            }
        });
}

/// Presence-filtered variant of @ref forEach.
///
/// Same callable shape, but only invokes it for entities whose mask
/// has all of the requested component bits set. The mask is checked
/// once per entity inside each chunk; job sizing is identical to
/// `forEach`. Use this in place of sentinel checks like `meshId < 0`.
template <typename... Components, typename F>
void forEachWith(SystemContext& ctx, F&& fn, std::uint32_t grain = 0) {
    static_assert(sizeof...(Components) > 0,
                  "forEachWith requires at least one required Component");
    const auto& world = ctx.world();
    const auto entities = world.entities();
    const auto count = static_cast<std::uint32_t>(entities.size());
    if (count == 0) return;

    auto spans = std::make_tuple(detail::getSpan<Components>(world)...);
    const auto masks = world.componentMasks();
    constexpr ComponentSet required = detail::requiredMask<Components...>();

    ctx.parallelFor(count, grain,
        [entities, masks, spans, required, fn = std::forward<F>(fn)]
        (Range r, CommandBuffer& cb) mutable {
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                if (!masks[i].hasAll(required)) continue;
                detail::invokeAt(fn, entities[i], spans, cb, i,
                                 std::index_sequence_for<Components...>{});
            }
        });
}

/// User-owned mask cache for @ref forEachWithCached.
///
/// `forEachWith<T...>` tests the required mask once per entity inside
/// the hot loop. When the same query runs many ticks in a row over a
/// world whose mask shape rarely changes, that test is wasted work.
/// `MaskCache` lets a system precompute the matching indices once (in
/// `preStep` or any other serial point) and then iterate them every
/// tick without re-testing.
///
/// Usage:
/// @code
/// class MySystem : public ISystem {
///   MaskCache cache_;
///   void preStep(SystemContext& ctx) override {
///       cache_.rebuild(ctx.world(), required<Transform, Velocity>());
///   }
///   void update(SystemContext& ctx) override {
///       forEachWithCached<Transform, Velocity>(ctx, cache_, body);
///   }
/// };
/// @endcode
///
/// **Invariants you own:** if the world's mask shape changed since the
/// last rebuild (spawn/destroy/addTag/setComponentMask in between),
/// the cache's indices become stale. Always rebuild in `preStep` if
/// your system's required set might match a new or removed entity
/// since the previous tick. The cache validates its indices are still
/// in range during iteration; out-of-range entries are skipped, but
/// the engine cannot detect an index that *is* in range yet no longer
/// matches the required mask.
///
/// The cache is *opt-in*: existing `forEachWith` call sites continue
/// to work and pay the per-entity test. The flag this batch refers to
/// is the choice between calling `forEachWith` vs.
/// `forEachWithCached` — there is no engine-wide setting.
class MaskCache {
public:
    /// Rebuild from the world's current dense arrays. Cost is O(N)
    /// over live entities. Safe to call from `preStep` or any other
    /// sim-thread point.
    void rebuild(const World& w, ComponentSet required) noexcept {
        indices_.clear();
        required_ = required;
        const auto masks = w.componentMasks();
        indices_.reserve(masks.size());
        for (std::uint32_t i = 0; i < masks.size(); ++i) {
            if (masks[i].hasAll(required)) indices_.push_back(i);
        }
    }

    /// Cached matching indices (in dense order at the time of
    /// @ref rebuild). Engine iterates this span in
    /// @ref forEachWithCached.
    std::span<const std::uint32_t> indices() const noexcept {
        return {indices_.data(), indices_.size()};
    }

    /// The required mask the cache was built against. Useful for
    /// asserts.
    ComponentSet required() const noexcept { return required_; }

    /// Number of cached entries.
    std::size_t size() const noexcept { return indices_.size(); }

    /// Drop the cached indices but keep the allocation.
    void clear() noexcept { indices_.clear(); }

private:
    std::vector<std::uint32_t> indices_;
    ComponentSet               required_;
};

/// Convenience: union of @ref Component bits for the listed types.
/// Pairs with @ref MaskCache::rebuild.
template <typename... Components>
constexpr ComponentSet required() noexcept {
    return detail::requiredMask<Components...>();
}

/// Cached variant of @ref forEachWith. Iterates the indices stashed
/// in @p cache instead of scanning every entity. The callable shape
/// is identical.
///
/// Skips entries whose cached index is now out of range
/// (entity was destroyed since rebuild). Does NOT re-test the per-
/// entity mask — the cache is the source of truth. Rebuild it in
/// `preStep` if your system's required set may match new entities
/// since the last tick.
template <typename... Components, typename F>
void forEachWithCached(SystemContext& ctx, const MaskCache& cache,
                       F&& fn, std::uint32_t grain = 0) {
    static_assert(sizeof...(Components) > 0,
                  "forEachWithCached requires at least one required Component");
    const auto& world = ctx.world();
    const auto entities = world.entities();
    const auto entityCount = static_cast<std::uint32_t>(entities.size());
    const auto cachedIndices = cache.indices();
    const auto cachedCount = static_cast<std::uint32_t>(cachedIndices.size());
    if (cachedCount == 0) return;

    auto spans = std::make_tuple(detail::getSpan<Components>(world)...);

    ctx.parallelFor(cachedCount, grain,
        [entities, entityCount, spans, cachedIndices,
         fn = std::forward<F>(fn)]
        (Range r, CommandBuffer& cb) mutable {
            for (std::uint32_t k = r.begin; k < r.end; ++k) {
                const std::uint32_t i = cachedIndices[k];
                if (i >= entityCount) continue;  // entity destroyed post-rebuild
                detail::invokeAt(fn, entities[i], spans, cb, i,
                                 std::index_sequence_for<Components...>{});
            }
        });
}

namespace detail {

template <typename C>
auto getChunkSpan(const internal::ArchetypeChunk& c) noexcept {
    if constexpr (std::is_same_v<C, Transform>)              return std::span<const Transform>(c.transforms);
    else if constexpr (std::is_same_v<C, Velocity>)          return std::span<const Velocity>(c.velocities);
    else if constexpr (std::is_same_v<C, RenderTag>)         return std::span<const RenderTag>(c.renderTags);
    else if constexpr (std::is_same_v<C, UserData>)          return std::span<const UserData>(c.userData);
    else if constexpr (std::is_same_v<C, Acceleration>)      return std::span<const Acceleration>(c.accelerations);
    else if constexpr (std::is_same_v<C, Parent>)            return std::span<const Parent>(c.parents);
    else if constexpr (std::is_same_v<C, Health>)            return std::span<const Health>(c.healths);
    else if constexpr (std::is_same_v<C, Faction>)           return std::span<const Faction>(c.factions);
    else if constexpr (std::is_same_v<C, AnimationStateRef>) return std::span<const AnimationStateRef>(c.animationStates);
    else if constexpr (std::is_same_v<C, PhysicsBodyRef>)    return std::span<const PhysicsBodyRef>(c.physicsBodies);
    else if constexpr (std::is_same_v<C, NavAgentRef>)       return std::span<const NavAgentRef>(c.navAgents);
    else if constexpr (std::is_same_v<C, BoundingVolume>)    return std::span<const BoundingVolume>(c.boundingVolumes);
    else static_assert(sizeof(C) == 0,
        "forEachChunk: component type must be one of the built-in data "
        "components — Transform, Velocity, RenderTag, UserData, "
        "Acceleration, Parent, Health, Faction, AnimationStateRef, "
        "PhysicsBodyRef, NavAgentRef, BoundingVolume.");
}

} // namespace detail

/// Parallel iteration over archetype chunks whose mask is a superset of
/// `required<Required...>()` (§3.1 batch 6).
///
/// Each invocation of @p fn receives:
///   - `std::span<const EntityHandle> entities` — the chunk's live
///     entity handles in row order;
///   - `std::span<const Required>... componentSpans` — one parallel
///     span per requested component, all of length
///     `entities.size()`;
///   - `CommandBuffer& cb` — the per-job recording buffer.
///
/// Compared with @ref forEachWith, this helper:
///   - skips the per-entity mask test in the hot loop (the chunk's
///     archetype mask is checked once per chunk, not per row);
///   - exposes the cache-friendly contiguous-per-archetype layout
///     directly, so SIMD-friendly inner loops can be written by hand
///     when the per-row cost matters;
///   - parallelizes over chunks (one job per chunk) rather than over
///     entities, so per-chunk work scales naturally with archetype
///     count.
///
/// @code
/// forEachChunk<Transform, Velocity>(ctx,
///     [dt](std::span<const EntityHandle> es,
///          std::span<const Transform> ts,
///          std::span<const Velocity> vs,
///          CommandBuffer& cb) {
///         for (std::size_t i = 0; i < es.size(); ++i) {
///             Transform next = ts[i];
///             next.position = ts[i].position + vs[i].linear * dt;
///             cb.setTransform(es[i], next);
///         }
///     });
/// @endcode
template <typename... Required, typename F>
void forEachChunk(SystemContext& ctx, F&& fn) {
    static_assert(sizeof...(Required) > 0,
                  "forEachChunk requires at least one required Component");
    const auto& world = ctx.world();
    constexpr ComponentSet req = detail::requiredMask<Required...>();
    const auto chunkCount = world.archetypeChunkCount();

    // Build the matching-chunk index list serially (cheap — typical
    // archetype counts are a few dozen). Then fan out over the matching
    // set so each worker processes a whole chunk.
    std::vector<std::size_t> matching;
    matching.reserve(chunkCount);
    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& c = world.archetypeChunk(i);
        if (c.entities.empty()) continue;
        if (!c.mask.hasAll(req)) continue;
        matching.push_back(i);
    }
    if (matching.empty()) return;

    const auto matchCount = static_cast<std::uint32_t>(matching.size());
    ctx.parallelFor(matchCount, /*grain*/ 1,
        [&world, matching, fn = std::forward<F>(fn)]
        (Range r, CommandBuffer& cb) mutable {
            for (std::uint32_t k = r.begin; k < r.end; ++k) {
                const auto& c = world.archetypeChunk(matching[k]);
                fn(std::span<const EntityHandle>(c.entities),
                   detail::getChunkSpan<Required>(c)...,
                   cb);
            }
        });
}

/// Single-threaded equivalent of @ref forEach.
///
/// Useful when the per-entity work is too small to parallelize, or
/// when the body needs to observe state across entities.
template <typename... Components, typename F>
void forEachSerial(SystemContext& ctx, F&& fn) {
    const auto& world = ctx.world();
    const auto entities = world.entities();
    const auto count = static_cast<std::uint32_t>(entities.size());
    if (count == 0) return;

    auto spans = std::make_tuple(detail::getSpan<Components>(world)...);

    ctx.single([entities, spans, fn = std::forward<F>(fn)]
        (Range, CommandBuffer& cb) mutable {
            for (std::uint32_t i = 0; i < entities.size(); ++i) {
                detail::invokeAt(fn, entities[i], spans, cb,
                                 static_cast<std::uint32_t>(i),
                                 std::index_sequence_for<Components...>{});
            }
        });
}

} // namespace threadmaxx
