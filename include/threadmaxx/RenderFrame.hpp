#pragma once

#include "Components.hpp"
#include "Handles.hpp"
#include "render/Camera.hpp"
#include "render/DebugGeometry.hpp"
#include "render/DrawItem.hpp"
#include "render/Light.hpp"
#include "render/RenderPasses.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace threadmaxx {

/// One renderable entity, flattened to a POD the renderer can consume
/// without touching engine internals.
///
/// @note Populated automatically by the engine for every entity with the
///       `RenderTag` presence bit (and not `DisabledTag`). The flat
///       `instances` array on @ref RenderFrame is the original Milestone-1
///       contract; richer rendering pipelines should consume the
///       hierarchical fields (@ref RenderFrame::cameras /
///       @ref RenderFrame::lights / @ref RenderFrame::drawItems) instead
///       — those are populated by user systems via the
///       @ref ISystem::buildRenderFrame hook.
struct RenderInstance {
    EntityHandle entity;
    Transform transform;
    std::int32_t meshId = -1;
    std::int32_t materialId = -1;
    std::uint32_t flags = 0;
    std::uint64_t userData = 0;
};

/// Snapshot the renderer sees for a given tick.
///
/// Owned by the engine; the renderer borrows it via `std::span` and
/// must not retain pointers across `submitFrame()` calls.
///
/// **Two consumption paths coexist:**
/// - The legacy flat @ref instances span — auto-populated by the engine
///   from every entity carrying a `RenderTag` presence bit (skipping
///   `DisabledTag`). Headless / minimal renderers can keep using just
///   this.
/// - The §3.2 hierarchical fields — @ref cameras, @ref lights,
///   @ref drawItems (per-pass), @ref debugLines, @ref debugPoints,
///   @ref debugText. These are populated by user systems via the
///   @ref ISystem::buildRenderFrame hook; the engine merges every
///   system's contribution in registration order on the simulation
///   thread.
///
/// Interpolation alpha: when `alpha == 0` the frame is the state at the
/// end of `tick`. When `0 < alpha < 1` the frame represents wall-clock
/// time `alpha * deltaTime` past that tick — i.e. between `tick` and
/// `tick+1`. Renderers that want smooth motion should cache the
/// previous frame's transforms (keyed by entity) and lerp
/// `prev -> current` by `alpha`.
struct RenderFrame {
    std::uint64_t tick = 0;
    double simulationTime = 0.0;
    double deltaTime = 0.0;
    float alpha = 0.0f;

    /// Auto-populated flat instance list — every entity with the
    /// `RenderTag` presence bit and without `DisabledTag`.
    std::span<const RenderInstance> instances;

    /// Cameras pushed by user systems via @ref RenderFrameBuilder.
    /// Order is registration order across systems, then insertion
    /// order within each system.
    std::span<const Camera> cameras;

    /// Lights, same ordering rules as @ref cameras.
    std::span<const Light> lights;

    /// Per-pass draw item bins. Index with @ref passIndex; the array
    /// is fixed-size at @ref kRenderPassCount.
    std::array<std::span<const DrawItem>, kRenderPassCount> drawItems = {};

    /// Debug geometry layers. Renderers without debug support ignore
    /// these.
    std::span<const DebugLine>  debugLines;
    std::span<const DebugPoint> debugPoints;
    std::span<const DebugText>  debugText;
};

} // namespace threadmaxx
