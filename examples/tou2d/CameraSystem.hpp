#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

namespace tou2d {

/// Emits a single orthographic camera each frame via the
/// `buildRenderFrame` hook. The camera follows the first
/// LocalPlayer-tagged ship; in M1 there is only one. Multi-camera
/// (shared dynamic frame-all or split-screen) lands in M3.
///
/// `setViewport(w, h)` is wired to GLFW's framebuffer-resize callback
/// from main.cpp so the aspect ratio stays correct.
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

private:
    UserComponentIds ids_;
    threadmaxx::Vec3 followTarget_ = {0.0f, 0.0f, 0.0f};
    std::uint32_t    viewportW_    = 1280;
    std::uint32_t    viewportH_    = 720;
    /// Half-height of the visible region in world units. Picked so a
    /// ~32-unit ship occupies ~10% of the visible height.
    float            orthoHalfH_   = 160.0f;
};

} // namespace tou2d
