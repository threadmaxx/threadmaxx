#pragma once

#include "CommandBuffer.hpp"
#include "Components.hpp"
#include "Handles.hpp"
#include "System.hpp"
#include "World.hpp"

#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace threadmaxx {

// Helpers that turn the "grab dense spans, write parallelFor by hand" pattern
// into a one-liner. Every live entity has every built-in component (the
// engine stores them in parallel arrays), so a query over a subset of
// component types is really "iterate all entities, hand me these refs".
//
//     forEach<Transform, Velocity>(ctx,
//         [dt](EntityHandle e, const Transform& t, const Velocity& v,
//              CommandBuffer& cb) {
//             Transform next = t;
//             next.position = t.position + v.linear * dt;
//             cb.setTransform(e, next);
//         });

namespace detail {

template <typename C>
auto getSpan(const World& w) noexcept {
    if constexpr (std::is_same_v<C, Transform>) return w.transforms();
    else if constexpr (std::is_same_v<C, Velocity>) return w.velocities();
    else if constexpr (std::is_same_v<C, RenderTag>) return w.renderTags();
    else if constexpr (std::is_same_v<C, UserData>)  return w.userData();
    else static_assert(sizeof(C) == 0,
        "forEach: component type must be one of Transform, Velocity, "
        "RenderTag, UserData");
}

template <typename C>
constexpr Component componentBit() noexcept {
    if constexpr (std::is_same_v<C, Transform>) return Component::Transform;
    else if constexpr (std::is_same_v<C, Velocity>) return Component::Velocity;
    else if constexpr (std::is_same_v<C, RenderTag>) return Component::RenderTag;
    else if constexpr (std::is_same_v<C, UserData>) return Component::UserData;
    else static_assert(sizeof(C) == 0,
        "forEachWith: component type must be one of Transform, Velocity, "
        "RenderTag, UserData");
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

// Parallel over all live entities. The callable is invoked as
//   fn(EntityHandle, const C0&, const C1&, ..., CommandBuffer&)
// with one CommandBuffer per worker chunk; the engine commits the buffers
// in submission order after the call returns.
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

// Presence-filtered variant. Same callable shape as forEach, but only invokes
// it for entities whose component mask has all of the requested component
// bits set. The mask is checked once per entity inside each chunk; the
// dispatch is still over `count` entities so jobs are sized identically to
// forEach. Use this in place of sentinel checks like `meshId < 0`.
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

// Single-threaded equivalent. Useful when the per-entity work is too small
// to parallelize, or when the body needs to observe state across entities.
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
