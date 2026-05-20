#pragma once

#include "CommandBuffer.hpp"
#include "Components.hpp"
#include "Handles.hpp"
#include "System.hpp"
#include "World.hpp"
#include "internal/Archetype.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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
        "forEachChunk/forEachWith: component type must be one of the built-in "
        "data components — Transform, Velocity, RenderTag, UserData, "
        "Acceleration, Parent, Health, Faction, AnimationStateRef, "
        "PhysicsBodyRef, NavAgentRef, BoundingVolume.");
}

/// §3.9.2 batch 17 — Inline-buffered list of matching-chunk indices.
///
/// Most worlds have under 32 archetype chunks, so a small array is
/// the common case. We spill into a heap vector only when the world
/// grows past that. Used by both @ref forEachWith and @ref forEachChunk
/// so the common case pays zero allocations per call.
class ChunkMatchList {
public:
    static constexpr std::size_t kInlineCap = 32;

    void push(std::size_t idx) {
        if (overflow_) {
            heap_.push_back(idx);
            return;
        }
        if (inlineSize_ < kInlineCap) {
            inline_[inlineSize_++] = idx;
            return;
        }
        // Spill to heap. Move the inline storage over so iteration
        // semantics stay simple (heap_ owns everything once overflowed).
        heap_.reserve(kInlineCap * 2);
        heap_.assign(inline_.begin(), inline_.begin() + inlineSize_);
        heap_.push_back(idx);
        overflow_ = true;
    }

    std::size_t size() const noexcept {
        return overflow_ ? heap_.size() : inlineSize_;
    }
    bool empty() const noexcept { return size() == 0; }
    std::size_t operator[](std::size_t i) const noexcept {
        return overflow_ ? heap_[i] : inline_[i];
    }

private:
    std::array<std::size_t, kInlineCap> inline_{};
    std::size_t                         inlineSize_ = 0;
    std::vector<std::size_t>            heap_;
    bool                                overflow_   = false;
};

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
/// Same callable shape, but only invokes for entities whose archetype
/// mask carries all requested component bits.
///
/// §3.9.2 batch 17 — internal path walks the `WorldView`'s cached
/// chunk pointers, skipping the stitched `componentMasks()` view AND
/// the per-entity mask check inside the hot loop. The chunk's mask is
/// checked **once per chunk** during the matching-list build; rows
/// inside a matching chunk are visited unconditionally. Public
/// behavior is identical to the pre-batch-17 implementation; the
/// callable still receives `(EntityHandle, const C0&, ..., CommandBuffer&)`.
template <typename... Components, typename F>
void forEachWith(SystemContext& ctx, F&& fn, std::uint32_t /*grain*/ = 0) {
    static_assert(sizeof...(Components) > 0,
                  "forEachWith requires at least one required Component");
    constexpr ComponentSet required = detail::requiredMask<Components...>();

    const auto chunks = ctx.worldView().chunks();
    detail::ChunkMatchList matching;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto* c = chunks[i];
        if (c == nullptr || c->entities.empty()) continue;
        if (!c->mask.hasAll(required)) continue;
        matching.push(i);
    }
    if (matching.empty()) return;

    const auto matchCount = static_cast<std::uint32_t>(matching.size());
    // Copy the match list into a stable per-call vector so the worker
    // lambda can capture it by value without dangling on the inline
    // buffer's storage. The vector is allocated only once per call and
    // is bounded by archetype count (typically a few dozen).
    std::vector<std::size_t> matchIndices;
    matchIndices.reserve(matchCount);
    for (std::uint32_t k = 0; k < matchCount; ++k) matchIndices.push_back(matching[k]);

    ctx.parallelFor(matchCount, /*grain*/ 1,
        [chunks, matchIndices = std::move(matchIndices),
         fn = std::forward<F>(fn)]
        (Range r, CommandBuffer& cb) mutable {
            for (std::uint32_t k = r.begin; k < r.end; ++k) {
                const auto* c = chunks[matchIndices[k]];
                const auto& entities = c->entities;
                auto chunkSpans = std::make_tuple(
                    detail::getChunkSpan<Components>(*c)...);
                const auto n = static_cast<std::uint32_t>(entities.size());
                for (std::uint32_t i = 0; i < n; ++i) {
                    detail::invokeAt(fn, entities[i], chunkSpans, cb, i,
                                     std::index_sequence_for<Components...>{});
                }
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

    /// §3.9.2 batch 17: pre-warm the index storage so the first
    /// @ref rebuild call after this point does not reallocate. Useful
    /// for systems that know the expected match count up front (e.g.
    /// "this query matches every entity in the world; reserve
    /// world.size()"). Subsequent rebuilds with capacity already in
    /// place are a `vector::clear` + `push_back` loop — no heap touch
    /// in steady state.
    void reserve(std::size_t cap) { indices_.reserve(cap); }

    /// Current backing-storage capacity, in elements.
    std::size_t capacity() const noexcept { return indices_.capacity(); }

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

/// §3.10.4 batch 28 — Row threshold above which `forEachChunk` splits a
/// matching chunk into multiple sub-jobs. Below the threshold a chunk
/// stays a single job (the pre-batch-28 behavior). Above it, the chunk
/// is partitioned into contiguous row ranges, each dispatched as its
/// own job so multiple workers can share the chunk's load.
///
/// Tuned against `bench/rpg_stress_bench` at 100k entities: the 100k-row
/// NPC chunk previously kept a single worker busy for the entire
/// `update` window (peakQueueDepth=1, waitSeconds/lastUpdateSeconds=99%).
/// With splitting, the chunk's rows fan out across all workers.
constexpr std::uint32_t kForEachChunkSubJobThreshold = 1024;

namespace detail {

/// §3.10.4 batch 28 — Per-sub-job descriptor for `forEachChunk` split.
/// One entry per dispatched job; `rowEnd > rowBegin` is enforced by the
/// builder. The chunk pointer is fetched at job time from the
/// captured-by-value `chunks` span, keeping the descriptor a 16-byte POD.
struct ChunkSubJob {
    std::size_t   chunkIdx = 0;
    std::uint32_t rowBegin = 0;
    std::uint32_t rowEnd   = 0;
};

/// §3.10.4 batch 28 — Compute the per-sub-job row count for a chunk of
/// `rowCount` rows on a `workers`-wide pool. Below the threshold the
/// chunk is one job; above, aim for ~`workers*8` sub-jobs per huge
/// chunk so steal still has work to grab when one worker stalls. The
/// 8× fanout (vs `pickGrain`'s 4× default) is empirically better for
/// chunk row counts ≫ threshold — more, smaller sub-jobs mean the
/// slowest worker holds the latch by a narrower margin. Floors the
/// per-job budget at the threshold so we never dispatch tiny sub-jobs.
inline std::uint32_t chunkSubJobBudget(std::uint32_t rowCount,
                                       std::uint32_t workers) noexcept {
    if (rowCount <= kForEachChunkSubJobThreshold) return rowCount;
    const std::uint32_t target = std::max(1u, workers * 8u);
    const std::uint32_t budget = (rowCount + target - 1u) / target;
    return std::max(kForEachChunkSubJobThreshold, budget);
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
/// §3.10.4 batch 28 — For chunks whose `entities.size() >
/// kForEachChunkSubJobThreshold`, the chunk is split into multiple
/// sub-jobs covering contiguous row ranges; the callback fires once per
/// sub-job with the chunk's spans narrowed to that range. Spans across
/// all invocations for the same chunk sum to the full chunk; every row
/// is still visited exactly once. The contract `es.size() ==
/// component_spans[k].size()` holds per call.
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
    constexpr ComponentSet req = detail::requiredMask<Required...>();

    // §3.9.2 batch 17 — read chunk pointers from the wave-scoped
    // `WorldView` instead of going through `World::archetypeChunk(i)`,
    // and stage the matching list in `ChunkMatchList` (small-buffer
    // optimized; no heap touch for the typical archetype counts).
    const auto chunks = ctx.worldView().chunks();
    detail::ChunkMatchList matching;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto* c = chunks[i];
        if (c == nullptr || c->entities.empty()) continue;
        if (!c->mask.hasAll(req)) continue;
        matching.push(i);
    }
    if (matching.empty()) return;

    // §3.10.4 batch 28 — Build a sub-job list. Most chunks contribute one
    // entry (the pre-batch-28 behavior); chunks above the threshold
    // contribute `ceil(rowCount / budget)` entries. The vector is
    // allocated once per call and is bounded by
    // (archetype count + biggest chunk's sub-job count).
    const std::uint32_t workers = ctx.workerCount();
    std::vector<detail::ChunkSubJob> subJobs;
    const auto matchCount = static_cast<std::uint32_t>(matching.size());
    subJobs.reserve(matchCount);
    for (std::uint32_t k = 0; k < matchCount; ++k) {
        const std::size_t ci = matching[k];
        const auto* c = chunks[ci];
        const auto rowCount =
            static_cast<std::uint32_t>(c->entities.size());
        const auto budget = detail::chunkSubJobBudget(rowCount, workers);
        for (std::uint32_t row = 0; row < rowCount; row += budget) {
            detail::ChunkSubJob sj;
            sj.chunkIdx = ci;
            sj.rowBegin = row;
            sj.rowEnd   = std::min(row + budget, rowCount);
            subJobs.push_back(sj);
        }
    }
    if (subJobs.empty()) return;

    const auto jobCount = static_cast<std::uint32_t>(subJobs.size());
    ctx.parallelFor(jobCount, /*grain*/ 1,
        [chunks, subJobs = std::move(subJobs),
         fn = std::forward<F>(fn)]
        (Range r, CommandBuffer& cb) mutable {
            for (std::uint32_t k = r.begin; k < r.end; ++k) {
                const auto& sj = subJobs[k];
                const auto* c = chunks[sj.chunkIdx];
                const auto len = static_cast<std::size_t>(
                    sj.rowEnd - sj.rowBegin);
                const std::span<const EntityHandle> es(
                    c->entities.data() + sj.rowBegin, len);
                fn(es,
                   detail::getChunkSpan<Required>(*c).subspan(
                       sj.rowBegin, len)...,
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
