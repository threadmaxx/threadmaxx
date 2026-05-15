#pragma once

#include <threadmaxx/Renderer.hpp>

#include <cstdint>
#include <memory>

struct GLFWwindow;

namespace threadmaxx { class Engine; }

namespace threadmaxx_vk {

/// §3.1 batch 9 — Vulkan 1.3 reference renderer.
///
/// Lives in `examples/vulkan_renderer/` and is treated as an example,
/// not as part of the core threadmaxx library. The static library
/// `threadmaxx::vulkan_renderer` exports just this header; everything
/// else is private to the implementation.
///
/// Construction: pass a GLFW window plus an Engine pointer. The engine
/// pointer is borrowed for `requestQuit()` signaling and for subscribing
/// to `AssetReloaded` events; the renderer does not own it. The window
/// is also borrowed — the user code that calls
/// `glfwCreateWindow(..., GLFW_NO_API, ...)` owns its lifetime.
///
/// Threading: every public method is sim-thread only, mirroring the
/// engine's `IRenderer` contract. The renderer does not spawn threads.
class VulkanRenderer : public threadmaxx::IRenderer {
public:
    struct Config {
        /// Initial swapchain extent. Re-fetched from the window's
        /// framebuffer size at every `onResize` / `recreateSwapchain`.
        std::uint32_t width  = 1280;
        std::uint32_t height = 720;
        /// Number of frames the renderer keeps in flight. The Vulkan
        /// swapchain typically owns 2 or 3 images; this matches.
        std::uint32_t framesInFlight = 2;
        /// Enable the standard Khronos validation layers if available.
        /// Silently ignored if the layer isn't installed.
        bool enableValidation = false;
    };

    VulkanRenderer(threadmaxx::Engine* engine,
                   GLFWwindow*         window,
                   Config              cfg);
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize() override;
    void shutdown()   override;
    void onResize(std::uint32_t width, std::uint32_t height) override;
    void submitFrame(const threadmaxx::RenderFrame& frame) override;

    /// Total number of frames the renderer has submitted to the GPU.
    /// Used by the smoke test and any HUD code reading it.
    std::uint64_t framesSubmitted() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx_vk
