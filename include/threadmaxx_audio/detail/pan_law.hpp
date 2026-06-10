#pragma once

/// @file pan_law.hpp
/// @brief Spatializer math — equal-power stereo pan + distance attenuation
/// + Doppler pitch shift. Pure functions; the mixer calls `computeSpatial()`
/// once per spatial voice per `mix()` call.

#include "threadmaxx_audio/spatial.hpp"

#include <cmath>

namespace threadmaxx::audio::detail {

inline float vec3Dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 vec3Cross(Vec3 a, Vec3 b) noexcept {
    return { a.y * b.z - a.z * b.y,
             a.z * b.x - a.x * b.z,
             a.x * b.y - a.y * b.x };
}
inline float vec3Length(Vec3 v) noexcept {
    return std::sqrt(vec3Dot(v, v));
}
inline Vec3 vec3Normalize(Vec3 v, Vec3 fallback = { 0.0f, 0.0f, 1.0f }) noexcept {
    const float len = vec3Length(v);
    if (len < 1e-9f) return fallback;
    return { v.x / len, v.y / len, v.z / len };
}

/// Per-voice spatial output. The mixer applies `gainL` and `gainR` to a
/// down-mixed mono source sample to write each output frame; `pitchShift`
/// scales the source-read cursor advance per output frame.
struct SpatialResult {
    float gainL      = 1.0f;
    float gainR      = 1.0f;
    float pitchShift = 1.0f;
};

/// Distance attenuation per `AttenuationModel`. Returns 1.0 at or inside
/// `minDistance`, 0.0 at or beyond `maxDistance`, follows the configured
/// curve in between.
inline float computeAttenuation(float dist, float minD, float maxD, AttenuationModel m) noexcept {
    if (dist <= minD) return 1.0f;
    if (dist >= maxD) return 0.0f;
    switch (m) {
        case AttenuationModel::Linear:
            return 1.0f - (dist - minD) / (maxD - minD);
        case AttenuationModel::Inverse: {
            const float g = minD / dist;
            return g > 1.0f ? 1.0f : g;
        }
        case AttenuationModel::InverseSquare: {
            const float r = minD / dist;
            const float g = r * r;
            return g > 1.0f ? 1.0f : g;
        }
    }
    return 0.0f;
}

/// Full per-voice spatial computation: attenuation, equal-power pan with
/// behind-attenuation, and Doppler pitch shift. `voiceLinearGain` is the
/// voice's own gain factor (the result already folds it in).
inline SpatialResult computeSpatial(const ListenerDesc& listener,
                                    const EmitterDesc&  emitter,
                                    float               voiceLinearGain) noexcept {
    SpatialResult out{};
    const Vec3 rel  = emitter.position - listener.position;
    const float dist = vec3Length(rel);

    const float atten = computeAttenuation(dist, emitter.minDistance,
                                           emitter.maxDistance, emitter.attenuationModel);

    // Listener basis. right = up × forward in a right-handed coord system
    // with +Z forward / +Y up — verified by `test_audio_spatial_pan`.
    const Vec3 forwardN = vec3Normalize(listener.forward);
    const Vec3 upN      = vec3Normalize(listener.up);
    const Vec3 rightN   = vec3Normalize(vec3Cross(upN, forwardN));

    Vec3 dirN{ 0.0f, 0.0f, 0.0f };
    if (dist > 1e-6f) {
        dirN = { rel.x / dist, rel.y / dist, rel.z / dist };
    }
    const float panX     = vec3Dot(dirN, rightN);     // -1 = left, +1 = right
    const float frontness = vec3Dot(dirN, forwardN);   // -1 = behind, +1 = in front

    // Equal-power pan: (panX + 1) * π/4 maps [-1, +1] → [0, π/2].
    constexpr float kQuarterPi = 0.785398163397448309616f;
    const float angle    = (panX + 1.0f) * kQuarterPi;
    const float panLeft  = std::cos(angle);
    const float panRight = std::sin(angle);

    // Behind attenuation: down to 0.7 when fully behind (frontness == -1).
    const float behindAtten = (frontness >= 0.0f) ? 1.0f : (1.0f + frontness * 0.3f);

    const float finalGain = atten * behindAtten * voiceLinearGain;
    out.gainL = finalGain * panLeft;
    out.gainR = finalGain * panRight;

    // Doppler. Conventional formula: f' = f · (c + v_listener_toward) /
    // (c + v_emitter_away). `dirN` points listener → emitter so dot(velocity,
    // dirN) > 0 means listener is moving TOWARD emitter / emitter is moving
    // AWAY from listener.
    if (dist > 1e-6f && emitter.dopplerFactor != 0.0f) {
        const float vListenerAlong = vec3Dot(listener.velocity, dirN);
        const float vEmitterAlong  = vec3Dot(emitter.velocity,  dirN);
        const float denom          = kSpeedOfSound + vEmitterAlong;
        if (denom > 1e-3f) {
            const float pitch = (kSpeedOfSound + vListenerAlong) / denom;
            out.pitchShift = 1.0f + (pitch - 1.0f) * emitter.dopplerFactor;
            if (out.pitchShift < 0.01f) out.pitchShift = 0.01f; // clamp pathological cases
        }
    }
    return out;
}

} // namespace threadmaxx::audio::detail
