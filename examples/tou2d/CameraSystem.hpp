#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>
#include <threadmaxx/render/Camera.hpp>

#include <array>
#include <cstdint>

namespace tou2d {

/// Emits one orthographic camera per LOCAL HUMAN ship via
/// `buildRenderFrame`. Bots never get a camera — only keyboard players
/// land in the split layout (M5.1).
///
/// Layout (chosen by `numHumans()`):
///   1 human  → full-screen `{0, 0, 1, 1}`
///   2 humans → left/right halves
///   3-4 humans → 2×2 grid (top-left, top-right, bot-left, bot-right);
///               the missing 4th quadrant in 3-player mode renders as
///               the framebuffer's clear color (black) since no camera
///               covers it.
///
/// `setViewport(w, h)` is wired to GLFW's framebuffer-resize callback
/// from main.cpp so per-camera aspect ratios stay correct.
class CameraSystem : public threadmaxx::ISystem {
public:
    explicit CameraSystem(UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.camera"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update(threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

    /// Called from the host on framebuffer resize. Stores width/height
    /// so the projection matrix uses the right aspect next frame.
    void setViewport(std::uint32_t width, std::uint32_t height) noexcept;

    /// M5.1 — how many of the slots `[0, numHumans)` are real cameras.
    /// Set by TouGame::onSetup BEFORE the first tick. Defaults to 1.
    void setNumHumans(std::uint8_t n) noexcept {
        numHumans_ = n > kMaxHumans ? kMaxHumans : n;
    }
    std::uint8_t numHumans() const noexcept { return numHumans_; }

    /// Latched at the end of `update()`. Readable by HudSystem so the
    /// HUD anchors to each human's view-corner without recomputing.
    threadmaxx::Vec3 followCenter(std::uint8_t humanSlot) const noexcept {
        return humanSlot < followTargets_.size()
                   ? followTargets_[humanSlot]
                   : threadmaxx::Vec3{0.0f, 0.0f, 0.0f};
    }
    /// Raw camera-design half-height. M5.7 — kept stable across layouts;
    /// the per-viewport effective value scales by viewport.h so a ship
    /// renders at the same pixel size in every split-screen mode.
    float orthoHalfH() const noexcept { return orthoHalfH_; }

    /// M5.7 — effective ortho half-height for the current layout.
    /// Equals `orthoHalfH_ * viewportFor(0).height`. All humans in any
    /// given layout share the same viewport height fraction so this is
    /// a single-value accessor rather than per-slot. Used by both
    /// `buildRenderFrame` (for the projection matrix) and `HudSystem`
    /// (for HUD anchor placement) so the two stay in sync.
    float effectiveOrthoHalfH() const noexcept;

    /// Aspect ratio of the per-camera viewport (NOT the framebuffer).
    /// Computed from the layout for `numHumans_`: 1 → full; 2 → half-
    /// width; 3-4 → half-width × half-height.
    float viewportAspect() const noexcept;

    /// Normalized viewport rect for one human slot in the current
    /// layout (4-tuple x, y, w, h ∈ [0, 1]). Slots outside `[0, numHumans_)`
    /// return `{0, 0, 0, 0}` (zero-area, never rendered).
    threadmaxx::Viewport viewportFor(std::uint8_t humanSlot) const noexcept;

private:
    UserComponentIds ids_;
    std::array<threadmaxx::Vec3, kMaxHumans> followTargets_{};
    std::uint8_t     numHumans_    = 1;
    std::uint32_t    viewportW_    = 1280;
    std::uint32_t    viewportH_    = 720;
    /// Half-height of the visible region in world units. Picked so a
    /// ~32-unit ship occupies ~10% of the visible height.
    float            orthoHalfH_   = 160.0f;
};

} // namespace tou2d
