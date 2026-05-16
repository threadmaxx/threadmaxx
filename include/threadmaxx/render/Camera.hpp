#pragma once

#include "../Components.hpp"  // Vec3

#include <array>
#include <cstdint>

namespace threadmaxx {

/// Projection mode for @ref Camera.
enum class ProjectionMode : std::uint8_t {
    Perspective = 0,
    Orthographic = 1,
};

/// §3.11.2 batch D2 — normalized viewport rect for multi-camera
/// rendering. Renderers map these (0..1) coordinates to a pixel
/// rect using the active swapchain extent. Default = full-screen
/// `{0, 0, 1, 1}`; pre-batch-D2 single-camera setups keep their
/// behavior bit-for-bit.
///
/// Y is "from the top": `{0, 0}` is the top-left corner, `{1, 1}` is
/// the bottom-right corner, matching CSS / GLFW window-coords
/// conventions. The Vulkan renderer flips Y for NDC internally as
/// before; this field is purely about subdividing the swapchain.
///
/// Cameras with overlapping viewports render in array order — later
/// cameras overdraw earlier ones in their viewport rect. Use this for
/// picture-in-picture HUDs / mini-maps.
struct Viewport {
    float x      = 0.0f;
    float y      = 0.0f;
    float width  = 1.0f;
    float height = 1.0f;
};

/// Render-side camera description.
///
/// Plain POD; the engine does not store cameras in @ref EntityStorage.
/// User systems push cameras into a @ref RenderFrameBuilder during the
/// @ref ISystem::buildRenderFrame hook. The renderer reads them off the
/// published @ref RenderFrame in registration order.
///
/// Matrices are stored column-major in the same convention shaders expect
/// (`view * world` for the view transform, `proj * view * world` for the
/// MVP). They are not interpreted by the engine — populate them however
/// your math library does and the renderer will consume them verbatim.
///
/// @par Why a POD instead of a built-in component?
/// Cameras are typically few in number and not aligned with the dense
/// parallel-array iteration model of @ref EntityStorage. Game code that
/// wants to attach a camera to a specific entity tracks the association
/// itself (typically by capturing the camera-owning @ref EntityHandle in
/// a system field) and emits a fresh @ref Camera each frame from
/// @ref ISystem::buildRenderFrame. This keeps the engine's renderer-
/// agnostic surface narrow.
struct Camera {
    ProjectionMode mode = ProjectionMode::Perspective;

    /// World-space position of the camera (informational; the view matrix
    /// is authoritative for shading).
    Vec3 position = {0.0f, 0.0f, 0.0f};

    /// World-space forward direction (informational).
    Vec3 forward = {0.0f, 0.0f, -1.0f};

    /// World-space up direction (informational).
    Vec3 up = {0.0f, 1.0f, 0.0f};

    /// Near clip plane.
    float nearZ = 0.1f;
    /// Far clip plane.
    float farZ = 1000.0f;

    /// Perspective FOV in radians (vertical). Ignored when @ref mode is
    /// @ref ProjectionMode::Orthographic.
    float fovYRadians = 1.0472f;  // ~60 degrees

    /// Aspect ratio (width / height). Renderer-side resize logic typically
    /// updates this each frame.
    float aspect = 1.7777778f;  // 16:9

    /// Orthographic half-height. Ignored when @ref mode is
    /// @ref ProjectionMode::Perspective.
    float orthoSize = 10.0f;

    /// Column-major view matrix.
    std::array<float, 16> view = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    /// Column-major projection matrix.
    std::array<float, 16> projection = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    /// Stable identifier the renderer can use to map a camera to a swap-
    /// chain image / target. The engine never interprets this value;
    /// game code chooses the encoding (e.g. a hash of camera name).
    std::uint32_t id = 0;

    /// §3.11.2 batch D2 — normalized viewport rect within the active
    /// render target. Default = full-screen. Multi-camera setups (HUD
    /// mini-maps, picture-in-picture aim cameras) override this so
    /// each camera lands in its own swapchain region.
    Viewport viewport = {};
};

} // namespace threadmaxx
