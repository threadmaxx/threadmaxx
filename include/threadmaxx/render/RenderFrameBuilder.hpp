#pragma once

#include "Camera.hpp"
#include "DebugGeometry.hpp"
#include "DrawItem.hpp"
#include "Light.hpp"
#include "RenderPasses.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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

    /// Append a debug-text entry referencing externally-owned storage.
    /// The producer is responsible for keeping the underlying
    /// characters alive until the renderer has consumed the frame
    /// (i.e. through the next `submitFrame` call).
    void addDebugText(const DebugText& t) { debugText_.push_back(t); }

    /// §3.6.5 batch 15a — owning-string overload. The string is copied
    /// into a per-builder arena owned by this builder; the resulting
    /// @ref DebugText::text view is valid for the lifetime of the
    /// next published @ref RenderFrame (until the engine resets this
    /// builder on the following tick).
    ///
    /// Use this for transient strings — @c std::format temporaries,
    /// stringified counters, etc. — where keeping the producer-side
    /// storage alive would be awkward.
    void addDebugText(const Vec3& position,
                      std::string_view text,
                      std::uint32_t colorRGBA = 0xFFFFFFFFu) {
        // Append the bytes; remember the slice extents and finalize the
        // view after we know the arena address is stable. Reserving on
        // first push keeps the steady-state allocation cost zero.
        const std::size_t off = stringArena_.size();
        stringArena_.append(text);
        debugText_.push_back(DebugText{position, std::string_view{}, colorRGBA});
        debugTextSlices_.push_back(OwnedSlice{
            static_cast<std::uint32_t>(debugText_.size() - 1),
            static_cast<std::uint32_t>(off),
            static_cast<std::uint32_t>(text.size())});
    }

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
        debugTextSlices_.clear();
        stringArena_.clear();
    }

    /// §3.6.5 batch 15a — resolve arena-backed debug-text views.
    /// Called by the engine's merge pass after the system's
    /// `buildRenderFrame` returns; rebinds slice indices to actual
    /// `std::string_view`s pointing into @ref stringArena_ now that
    /// its address is stable (the user's hook may have triggered a
    /// reallocation as it pushed). After this call, every entry in
    /// @ref debugText with the matching slice is fixed up; entries
    /// pushed via the producer-owned `addDebugText(const DebugText&)`
    /// overload are left untouched.
    void finalizeDebugTextViews() noexcept {
        for (const auto& s : debugTextSlices_) {
            debugText_[s.entryIndex].text = std::string_view(
                stringArena_.data() + s.offset, s.length);
        }
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
    struct OwnedSlice {
        std::uint32_t entryIndex = 0;
        std::uint32_t offset     = 0;
        std::uint32_t length     = 0;
    };

    std::vector<Camera>      cameras_;
    std::vector<Light>       lights_;
    std::array<std::vector<DrawItem>, kRenderPassCount> drawItems_;
    std::vector<DebugLine>   debugLines_;
    std::vector<DebugPoint>  debugPoints_;
    std::vector<DebugText>   debugText_;

    // §3.6.5 batch 15a — owning-string arena for the
    // `addDebugText(Vec3, string_view, color)` overload. Holds the
    // packed bytes; `debugTextSlices_` records the slices for fixup
    // in `finalizeDebugTextViews`.
    std::string                stringArena_;
    std::vector<OwnedSlice>    debugTextSlices_;
};

} // namespace threadmaxx
