#pragma once

#include "threadmaxx_animation/types.hpp"

#include <string>
#include <vector>

namespace threadmaxx::animation {

/// A single sampled pose-at-time pair. The reference to the pose is
/// opaque — A2 will introduce the curve storage that ties PoseId to
/// authored keyframes.
struct ClipSample {
    PoseId pose{};
    float time{};
};

/// Time-keyed event fired by clip sampling. Time is in seconds
/// within `[0, ClipDesc::duration]`.
struct EventTrackEvent {
    float time{};
    std::string name;
};

/// Clip metadata. Curve data is not yet here in A1 — A2 adds the
/// per-channel sampling backbone. The metadata round-trip alone is
/// what A1 tests, so this POD is the contract the rest of the data
/// model can already build against.
struct ClipDesc {
    std::string name;
    float duration{};
    bool looping{};
    std::vector<EventTrackEvent> events;
};

} // namespace threadmaxx::animation
