#pragma once

#include "Camera.hpp"
#include "DebugGeometry.hpp"
#include "DrawItem.hpp"
#include "Light.hpp"
#include "RenderPasses.hpp"

#include <array>
#include <span>
#include <vector>

namespace threadmaxx {

/// One system's contribution to the next @ref RenderFrame.
///
/// The engine hands each registered system its own builder during the
/// new @ref ISystem::buildRenderFrame hook. Systems push cameras,
/// lights, per-pass draw items, and debug geometry; the engine merges
/// every system's builder into the final frame in registration order on
/// the simulation thread (deterministic — same input order, same
/// merged output).
///
/// The builder is owned by the engine for the duration of the hook and
/// is not safe to retain across ticks. Internally it holds plain
/// `std::vector`s; steady-state usage pays zero allocations after the
/// first tick because the engine reuses the storage.
class RenderFrameBuilder {
public:
    RenderFrameBuilder() = default;
    RenderFrameBuilder(const RenderFrameBuilder&) = delete;
    RenderFrameBuilder& operator=(const RenderFrameBuilder&) = delete;
    RenderFrameBuilder(RenderFrameBuilder&&) noexcept = default;
    RenderFrameBuilder& operator=(RenderFrameBuilder&&) noexcept = default;

    /// Append a @ref Camera. Cameras land in the final frame in the
    /// order they were added, with all of this system's cameras
    /// preceding the next system's (which preserves registration
    /// order across systems).
    void addCamera(const Camera& c) { cameras_.push_back(c); }

    /// Append a @ref Light. Same ordering rules as @ref addCamera.
    void addLight(const Light& l) { lights_.push_back(l); }

    /// Append a @ref DrawItem to @p pass's bin.
    void addDrawItem(RenderPass pass, const DrawItem& item) {
        drawItems_[passIndex(pass)].push_back(item);
    }

    /// Append a debug line. Renderers that don't support debug geometry
    /// simply ignore the @ref RenderFrame's debug spans.
    void addDebugLine(const DebugLine& l) { debugLines_.push_back(l); }
    void addDebugPoint(const DebugPoint& p) { debugPoints_.push_back(p); }
    void addDebugText(const DebugText& t) { debugText_.push_back(t); }

    /// Clear all per-system slices, preserving the underlying
    /// allocations. Called by the engine each tick before invoking
    /// each system's hook.
    void reset() noexcept {
        cameras_.clear();
        lights_.clear();
        for (auto& bin : drawItems_) bin.clear();
        debugLines_.clear();
        debugPoints_.clear();
        debugText_.clear();
    }

    /// @internal Read-only access for the engine's merge pass.
    std::span<const Camera>      cameras()  const noexcept { return cameras_; }
    std::span<const Light>       lights()   const noexcept { return lights_; }
    std::span<const DrawItem>    drawItems(RenderPass p) const noexcept {
        return drawItems_[passIndex(p)];
    }
    std::span<const DebugLine>   debugLines()  const noexcept { return debugLines_; }
    std::span<const DebugPoint>  debugPoints() const noexcept { return debugPoints_; }
    std::span<const DebugText>   debugText()   const noexcept { return debugText_; }

private:
    std::vector<Camera>      cameras_;
    std::vector<Light>       lights_;
    std::array<std::vector<DrawItem>, kRenderPassCount> drawItems_;
    std::vector<DebugLine>   debugLines_;
    std::vector<DebugPoint>  debugPoints_;
    std::vector<DebugText>   debugText_;
};

} // namespace threadmaxx
