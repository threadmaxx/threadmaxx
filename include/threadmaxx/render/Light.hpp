#pragma once

#include "../Components.hpp"  // Vec3

#include <cstdint>

namespace threadmaxx {

/// Source-of-illumination tag for @ref Light.
enum class LightType : std::uint8_t {
    Directional = 0,  ///< Infinitely-distant; only @ref Light::direction matters.
    Point       = 1,  ///< Omni; @ref Light::position + @ref Light::range.
    Spot        = 2,  ///< Cone; @ref Light::position, direction, range, cones.
};

/// Render-side light description.
///
/// Plain POD; not stored in @ref EntityStorage. User systems push lights
/// into a @ref RenderFrameBuilder during @ref ISystem::buildRenderFrame.
/// Field meaning depends on @ref Light::type:
///
/// - **Directional**: only @ref direction and @ref color/@ref intensity
///   are read. @ref position is informational (for editor overlays).
/// - **Point**: @ref position, @ref color, @ref intensity, @ref range.
/// - **Spot**: all of the above plus @ref direction, @ref innerConeRadians,
///   @ref outerConeRadians.
struct Light {
    LightType type = LightType::Directional;

    /// World-space position. Ignored for directional lights.
    Vec3 position = {0.0f, 0.0f, 0.0f};

    /// World-space direction. For point lights this is informational.
    Vec3 direction = {0.0f, -1.0f, 0.0f};

    /// Linear RGB color. Multiplied by @ref intensity at shading time.
    Vec3 color = {1.0f, 1.0f, 1.0f};

    /// Scalar intensity multiplier. Units are renderer-defined (lumens,
    /// candela, raw multiplier — pick one in your shader and stay
    /// consistent).
    float intensity = 1.0f;

    /// Distance at which attenuation reaches zero. Ignored for
    /// directional lights.
    float range = 10.0f;

    /// Inner cone half-angle in radians (full intensity within). Ignored
    /// for non-spot lights.
    float innerConeRadians = 0.2618f;  // ~15 degrees

    /// Outer cone half-angle in radians (falloff edge). Ignored for
    /// non-spot lights.
    float outerConeRadians = 0.5236f;  // ~30 degrees

    /// Hint for the renderer: this light should cast a shadow if the
    /// pipeline supports it. The engine itself does no shadow work.
    bool castsShadow = false;
};

} // namespace threadmaxx
