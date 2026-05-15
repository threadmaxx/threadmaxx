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
#include <optional>
#include <span>

namespace threadmaxx {

/// §3.6.5 batch 15a — last-tick transform for one render-eligible
/// entity. Same dense-index ordering as @ref RenderFrame::instances:
/// `prevTransforms[i]` is the previous-tick transform of the entity in
/// `instances[i]`, or the entity's current transform on the first
/// frame it became visible (so renderers can lerp `prev → current` by
/// @ref RenderFrame::alpha without a special-case branch).
struct RenderInstancePrev {
    EntityHandle entity;
    Transform    transform;
};

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

    /// §3.6.5 batch 15a — previous-tick transforms paired with
    /// @ref instances. Same length and same dense-index ordering; for
    /// every `i`, `prevTransforms[i].entity == instances[i].entity` and
    /// the transform is the entity's value at the end of tick
    /// `tick - 1` (or this tick's value if the entity is appearing for
    /// the first time — gives a clean `lerp(prev, current, alpha)`
    /// with no special case on spawn).
    std::span<const RenderInstancePrev> prevTransforms;

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

    /// §3.6.5 batch 15a — map a @ref Camera::id to its index in
    /// @ref cameras (which is also the bit position used in
    /// @ref DrawItem::cameraMask). Linear scan; cheap for the small
    /// camera counts typical at this point (≤ 32 by the mask cap).
    ///
    /// Returns `std::nullopt` if no camera with the given id is in the
    /// frame, or if more than @ref kMaxCameras have been pushed (the
    /// excess are not addressable through `cameraMask`).
    std::optional<std::uint8_t> cameraIndexById(std::uint32_t id) const noexcept {
        const std::size_t n = cameras.size() < 32 ? cameras.size() : 32;
        for (std::size_t i = 0; i < n; ++i) {
            if (cameras[i].id == id) {
                return static_cast<std::uint8_t>(i);
            }
        }
        return std::nullopt;
    }
};

/// §3.6.5 batch 15a — bit-width of @ref DrawItem::cameraMask. The first
/// 32 cameras pushed by user systems are addressable through the mask;
/// beyond that, `cullByFrustum` and any other mask-writing code must
/// drop the excess.
inline constexpr std::size_t kMaxCameras = 32;

} // namespace threadmaxx
