#pragma once

#include <threadmaxx/Renderer.hpp>
#include <threadmaxx/Resource.hpp>
#include <threadmaxx_vk/Mesh.hpp>

#include <cstdint>
#include <memory>
#include <span>

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

    /// §3.11 batch 9b.2 — replace the cached "default mesh" used to
    /// draw every RenderTag instance. Pre-batch-9b.2 the default was
    /// the procedural unit-cube created in `initialize()`; game code
    /// may now call this after `initialize()` returns to swap in a
    /// mesh sourced from a real `.obj` asset (or any other content
    /// pipeline). Passing a default-constructed (invalid) handle
    /// drops the override and the renderer skips opaque drawing for
    /// instances whose mesh isn't otherwise resolved.
    ///
    /// The handle is held by-value, so the slot's refcount is bumped
    /// for the lifetime of the override. The previous handle (if
    /// any) is reset, which decrements its refcount; if no other
    /// holder pins it, the slot frees automatically. Per the
    /// `ResourceHandle` contract the GPU memory itself stays
    /// allocated until the loader's `releaseGpuResources` runs at
    /// renderer shutdown.
    void setDefaultMesh(threadmaxx::ResourceHandle<Mesh> handle) noexcept;

    /// §3.11 batch 9b.2 — convenience overload that uploads the given
    /// vertex / index data via the renderer's internal `MeshLoader`
    /// and then installs the resulting handle as the default mesh.
    /// Same vertex layout contract as `setDefaultMesh`: binding 0 is
    /// pos[3] + normal[3], 24-byte stride; indices are 16-bit. Returns
    /// `false` if the upload fails (e.g. layout violation, allocation
    /// failure); the previous default stays in place.
    bool setDefaultMeshFromData(std::span<const float>         vertices,
                                std::span<const std::uint16_t> indices) noexcept;

    /// §3.11 batch 9b.2b — register a non-default mesh and obtain a
    /// non-negative meshId game code can stamp onto `DrawItem::meshId`
    /// / `RenderInstance::meshId`. The renderer's draw loop groups
    /// instances by meshId and binds the matching mesh per group; an
    /// instance with `meshId == 0` (or `< 0`) falls back to the
    /// default mesh installed via `setDefaultMesh`. Returns the new
    /// meshId, or `-1` on null input.
    std::int32_t registerMesh(threadmaxx::ResourceHandle<Mesh> handle);

    /// Convenience: uploads `vertices` / `indices` via the internal
    /// `MeshLoader` and registers the resulting handle in one call.
    /// Same layout contract as `setDefaultMeshFromData`. Returns the
    /// new meshId, or `-1` on upload failure.
    std::int32_t registerMeshFromData(std::span<const float>         vertices,
                                      std::span<const std::uint16_t> indices) noexcept;

    /// §3.11 batch 9b.3 — trigger a hot reload for every shader the
    /// renderer's pipelines depend on. For each tracked shader the
    /// engine's `markResourceStale<Shader>(id)` is invoked; the
    /// `ShaderLoader` then re-reads the on-disk `.spv` on its next
    /// `update()` tick and emits `AssetReloaded`. The renderer's
    /// internal subscriber rebuilds the affected `VkPipeline` in
    /// place. Useful as an F-key handler in a debug UI.
    void reloadShaders();

    /// §3.11.7b.5 batch 9b.4.b — register a skinned mesh. Vertex
    /// stream MUST match the skinned pipeline's layout: 56 bytes
    /// per vertex = `pos[3]f + normal[3]f + boneIDs[4]u32 +
    /// boneWeights[4]f`. Returns a non-negative `skinnedMeshId` to
    /// use in `DrawItem::meshId` for items that also set
    /// `DrawItem::skeletonId >= 0` (the dispatch flag). Returns
    /// `-1` on upload failure.
    std::int32_t registerSkinnedMeshFromData(
        std::span<const float>         vertices,
        std::span<const std::uint16_t> indices) noexcept;

    /// M2.8 — install a fullscreen background texture rendered behind
    /// all opaque + debug draws. `rgba` is tightly-packed RGBA8
    /// (R,G,B,A; 4 bytes per pixel; row-major; row 0 = top of the
    /// image — i.e. stb_image's default decode order). Blocks on a
    /// `vkDeviceWaitIdle` + GPU upload + fence; safe to call between
    /// frames at level-load time. Passing `rgba.empty()` (or zero
    /// extent) clears the background. Returns true on success.
    ///
    /// Subsequent calls release the previous backdrop's refcount
    /// before installing the new one. Resampler / view / image stay
    /// alive as long as the renderer's `ResourceHandle<Texture>`
    /// pins them; freed at `shutdown()` via the loader.
    bool setBackgroundFromRgba(std::span<const std::uint8_t> rgba,
                               std::uint32_t                 width,
                               std::uint32_t                 height);

    /// M3.2 — partial-rect update of the currently-installed background
    /// texture. `rgba` is tightly-packed RGBA8, `w * h * 4` bytes,
    /// covering pixels `[x, x+w) × [y, y+h)`. The image stays in place
    /// (no realloc, no descriptor rewrite), so this is the destruction
    /// hot path: per-tile destroys are O(tile area) bytes, not
    /// O(image area), and pipeline through the graphics queue's own
    /// barriers — no `vkDeviceWaitIdle`. Returns false if no
    /// background is installed or the region falls outside the image.
    bool updateBackgroundRegion(std::uint32_t                 x,
                                std::uint32_t                 y,
                                std::uint32_t                 w,
                                std::uint32_t                 h,
                                std::span<const std::uint8_t> rgba);

    /// M3.2 — placement of the world-space quad the background texture
    /// is painted onto. The quad spans
    /// `[centerX - halfW, centerX + halfW] × [centerY - halfH, centerY + halfH]`
    /// at world z = -1, sampled via camera 0's viewProj. Center
    /// defaults to (0, 0) — pass non-zero when the cell grid is not
    /// symmetric around the world origin (e.g. even cellsX / cellsY
    /// counts produce a half-tile offset).
    void setBackgroundWorldExtent(float halfW, float halfH,
                                  float centerX = 0.0f,
                                  float centerY = 0.0f) noexcept;

    /// M4.8 — install a transparent foreground texture rendered AFTER
    /// the opaque pass / debug overlays. Twin of `setBackgroundFromRgba`;
    /// same RGBA8 layout, same blocking-upload semantics. The
    /// foreground draws as a world-space quad placed via
    /// `setForegroundWorldExtent`, blended onto the back buffer with
    /// straight alpha. Used by tou2d to overlay decoded SHP sprites at
    /// each ship's current rotation without touching the cube path.
    bool setForegroundFromRgba(std::span<const std::uint8_t> rgba,
                               std::uint32_t                 width,
                               std::uint32_t                 height);

    /// M4.8 — partial-rect update of the foreground texture. Same
    /// contract as `updateBackgroundRegion`. Hot path: the per-tick
    /// sprite compositor flushes one bbox covering every ship that
    /// moved or changed rotation since last frame.
    bool updateForegroundRegion(std::uint32_t                 x,
                                std::uint32_t                 y,
                                std::uint32_t                 w,
                                std::uint32_t                 h,
                                std::span<const std::uint8_t> rgba);

    /// M4.8 — world-space placement of the foreground quad. Typically
    /// matches the background extent exactly so foreground pixels line
    /// up with background pixels 1:1.
    void setForegroundWorldExtent(float halfW, float halfH,
                                  float centerX = 0.0f,
                                  float centerY = 0.0f) noexcept;

    /// 2026-06-01 — install a "sky" parallax texture rendered BEFORE the
    /// background quad. Same RGBA8 layout + blocking-upload semantics as
    /// `setBackgroundFromRgba`. The sky is placed via
    /// `setSkyWorldExtent`; by sizing the sky quad larger than the level
    /// extent (e.g. by the level's `parallaxat` multiplier), camera
    /// traversal of the level only covers `1/multiplier` of the sky
    /// image's UV range — yielding a naturally slower visual scroll
    /// without per-frame updates.
    ///
    /// To make the sky visible *through* the level, callers must mark
    /// Air pixels in the background texture with `alpha=0`; the sky
    /// then shows through wherever the background's alpha is zero.
    /// Reuses the background pipeline (same shader, same set layout,
    /// straight-alpha blend) — one extra draw call per camera.
    ///
    /// Returns true on success; `rgba.empty()` (or zero extent) clears
    /// the sky and disables the draw entirely.
    /// @thread_safety not thread-safe; call from the sim / setup thread
    ///                between frames. Blocks on `vkDeviceWaitIdle`.
    bool setSkyFromRgba(std::span<const std::uint8_t> rgba,
                        std::uint32_t                 width,
                        std::uint32_t                 height);

    /// 2026-06-01 — world-space placement of the sky quad. Centered on
    /// (`centerX`, `centerY`) with half-extents (`halfW`, `halfH`). For
    /// a level whose terrain spans `[-LhalfW, +LhalfW] × [-LhalfH, +LhalfH]`
    /// and a `parallaxAt` multiplier `P` from the .lev config, pass
    /// `halfW = LhalfW × P, halfH = LhalfH × P` so the camera's
    /// traversal of the level walks `1/P` of the sky image — i.e. the
    /// sky scrolls `P×` slower than the terrain.
    void setSkyWorldExtent(float halfW, float halfH,
                           float centerX = 0.0f,
                           float centerY = 0.0f) noexcept;

    /// M6.0b — install a transparent UI overlay texture rendered ONCE
    /// per frame at screen-space NDC AFTER every per-camera pass (above
    /// background, opaque, debug, and foreground). Unlike background /
    /// foreground (which are world-anchored quads transformed by the
    /// camera's viewProj), the UI overlay covers the full swapchain
    /// extent and ignores any camera setup — UI pixels are in screen
    /// coordinates by construction.
    ///
    /// `rgba` is tightly-packed RGBA8 (R,G,B,A; 4 bytes per pixel; row-
    /// major; row 0 = top of the image). Blocks on a `vkDeviceWaitIdle`
    /// + GPU upload + fence — same semantics as `setBackgroundFromRgba`.
    /// Passing `rgba.empty()` (or zero extent) clears the overlay (the
    /// renderer skips the draw entirely until a non-empty rgba is
    /// installed). Returns true on success.
    ///
    /// Reuses the existing background pipeline (alpha-blended, depth-
    /// off, cull-none). One extra draw call per frame; cost is the
    /// texture sample over the swapchain extent plus the typical 6-vert
    /// quad overhead.
    bool setUiOverlayFromRgba(std::span<const std::uint8_t> rgba,
                              std::uint32_t                 width,
                              std::uint32_t                 height);

    /// M6.0b — partial-rect update of the UI overlay texture. Same
    /// contract as `updateBackgroundRegion` — `rgba` is `w*h*4` bytes
    /// covering `[x, x+w) × [y, y+h)`. The hot path: a UI compositor
    /// CPU-blits glyph quads into a CPU framebuffer, flushes the dirty
    /// bbox here every tick. No `vkDeviceWaitIdle`; the graphics queue's
    /// own barriers handle synchronization. Returns false if no overlay
    /// is installed or the region falls outside the image.
    bool updateUiOverlayRegion(std::uint32_t                 x,
                               std::uint32_t                 y,
                               std::uint32_t                 w,
                               std::uint32_t                 h,
                               std::span<const std::uint8_t> rgba);

    /// M6.0b — toggle UI overlay rendering without releasing the
    /// installed texture. Useful for `F3`-style debug-overlay toggles
    /// where the bitmap stays GPU-resident across frames. Default is
    /// `true` once a texture is installed (a fresh install also flips
    /// the flag back on).
    void setUiOverlayEnabled(bool enabled) noexcept;

    /// 2026-06-15 — opt-in Dear ImGui integration (dynamic-rendering
    /// path). Available only when threadmaxx was built with the editor's
    /// ImGui FetchContent option turned on (the static library auto-
    /// defines `THREADMAXX_VK_HAS_IMGUI=1` in that case). When the
    /// define is absent, every method below is a no-op and returns
    /// `false` / does nothing, so hosts can compile against the public
    /// API regardless of how the engine was built.
    ///
    /// Lifecycle:
    ///   1. `vk.initialize()`            ← renderer up.
    ///   2. `vk.initializeImGui()`        ← creates the ImGui context,
    ///                                       wires the GLFW + Vulkan
    ///                                       backends, uploads the
    ///                                       default font texture.
    ///   3. each tick, BEFORE the panel widgets are built:
    ///         `vk.beginImGuiFrame();`
    ///         // host calls `ImGui::Begin(...) / Text / End`
    ///         `vk.endImGuiFrame();`   // ImGui::Render()
    ///   4. `engine.step()` → `submitFrame()` paints the cached
    ///      ImDrawData onto the swapchain inside the same dynamic-
    ///      rendering pass that draws the world.
    ///   5. `vk.shutdownImGui();`        ← before `vk.shutdown()`.
    ///                                     idempotent.
    ///
    /// Threading: all sim-thread, same as the rest of `IRenderer`.
    bool initializeImGui();
    void beginImGuiFrame() noexcept;
    void endImGuiFrame() noexcept;
    void shutdownImGui() noexcept;
    /// True between a successful `initializeImGui()` and a matching
    /// `shutdownImGui()` (or `shutdown()`).
    bool imguiInitialized() const noexcept;

    /// §3.11.7b.5 batch 9b.4.b — upload the per-frame bone matrices.
    /// `matrices` is a packed array of `mat4` values, column-major
    /// (Vulkan std140 convention). The renderer copies into the
    /// current back PerFrame's bone buffer and updates the descriptor
    /// set; the buffer is consumed by the next `submitFrame` call.
    /// Per-`DrawItem::pose.ringSlot` index addresses into this array
    /// (`boneBase = pose.ringSlot`).
    ///
    /// Call this each tick from the sim thread BEFORE
    /// `engine.step()` for the corresponding tick; the engine's
    /// render-frame build runs inside `step()` and `submitFrame`
    /// fires immediately after with the freshly-written bone buffer.
    void setBoneMatrices(std::span<const float> matrices) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx_vk
