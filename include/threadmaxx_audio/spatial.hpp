#pragma once

/// @file spatial.hpp
/// @brief 3D audio state — listener and emitter descriptors, attenuation
/// curves, and the speed-of-sound constant. AU4 wires this state into the
/// mixer's per-voice spatial path; the math helpers live in
/// `detail/pan_law.hpp`.

#include <cstdint>

namespace threadmaxx::audio {

/// 3D vector — right-handed, X-right / Y-up / Z-forward by convention. POD;
/// trivial copy / no constructors. Game code can adapt its own vector type
/// at the call site.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline constexpr bool operator==(Vec3 a, Vec3 b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline constexpr bool operator!=(Vec3 a, Vec3 b) noexcept { return !(a == b); }
inline constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline constexpr Vec3 operator*(Vec3 a, float s) noexcept { return {a.x*s, a.y*s, a.z*s}; }

/// Distance-attenuation curve. The mixer picks per-emitter.
enum class AttenuationModel : std::uint8_t {
    Linear        = 0,  ///< gain = 1 - (d - min) / (max - min)
    Inverse       = 1,  ///< gain = min / d, clamped to [0, 1]
    InverseSquare = 2,  ///< gain = (min / d)^2, clamped to [0, 1]
};

/// 3D listener pose. Position drives distance attenuation; orientation
/// (forward + up) drives the stereo pan direction. Velocity feeds Doppler.
struct ListenerDesc {
    Vec3 position;
    Vec3 velocity;
    Vec3 forward { 0.0f, 0.0f, 1.0f };
    Vec3 up      { 0.0f, 1.0f, 0.0f };
};

/// 3D emitter state attached to a voice. `dopplerFactor=0` disables Doppler
/// even when velocities are non-zero — games that want spatial pan without
/// pitch shift opt out this way.
struct EmitterDesc {
    Vec3             position;
    Vec3             velocity;
    float            minDistance      = 1.0f;
    float            maxDistance      = 50.0f;
    float            dopplerFactor    = 1.0f;
    AttenuationModel attenuationModel = AttenuationModel::Linear;
};

/// Speed of sound, m/s. Used by the Doppler formula in
/// `detail/pan_law.hpp::computeSpatial`. Constant rather than a config field
/// because games virtually never tune it.
inline constexpr float kSpeedOfSound = 343.0f;

} // namespace threadmaxx::audio
