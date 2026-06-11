#pragma once

/// @file scene.hpp
/// @brief Thin convenience helpers for engine integration. These wrap the
/// AU2/AU4 setListener/setEmitter calls in named-parameter form so a game's
/// audio system can stay close to one-line-per-update.
///
/// The library stays engine-agnostic — these helpers don't reference any
/// engine type. Callers convert their own `Transform` / `Velocity` to
/// `audio::Vec3` at the call site, which is one struct-init line each.

#include "threadmaxx_audio/mixer.hpp"
#include "threadmaxx_audio/spatial.hpp"

namespace threadmaxx::audio {

/// One-line listener update. Wraps an `AudioMixer::setListener` call —
/// useful inside a per-tick `ISystem::update` that reads world Transform /
/// Velocity and feeds them to the mixer.
inline void setListenerPose(AudioMixer& mixer, ListenerId listener,
                            Vec3 position,
                            Vec3 velocity = { 0.0f, 0.0f, 0.0f },
                            Vec3 forward  = { 0.0f, 0.0f, 1.0f },
                            Vec3 up       = { 0.0f, 1.0f, 0.0f }) noexcept {
    ListenerDesc d{};
    d.position = position;
    d.velocity = velocity;
    d.forward  = forward;
    d.up       = up;
    mixer.setListener(listener, d);
}

/// One-line emitter update. Wraps `AudioMixer::setEmitter` — pose changes
/// per tick, the attenuation/Doppler knobs are usually constant after the
/// initial attach.
inline void setEmitterPose(AudioMixer& mixer, VoiceId voice, ListenerId listener,
                           Vec3 position,
                           Vec3 velocity = { 0.0f, 0.0f, 0.0f },
                           float minDistance      = 1.0f,
                           float maxDistance      = 50.0f,
                           float dopplerFactor    = 1.0f,
                           AttenuationModel model = AttenuationModel::Linear) noexcept {
    EmitterDesc e{};
    e.position         = position;
    e.velocity         = velocity;
    e.minDistance      = minDistance;
    e.maxDistance      = maxDistance;
    e.dopplerFactor    = dopplerFactor;
    e.attenuationModel = model;
    mixer.setEmitter(voice, listener, e);
}

} // namespace threadmaxx::audio
