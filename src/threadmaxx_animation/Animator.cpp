#include "threadmaxx_animation/eval.hpp"

#include "threadmaxx_animation/detail/curve_eval.hpp"

#include <cmath>

namespace threadmaxx::animation {

void Animator::setClip(const ClipDesc* clip) noexcept {
    clip_ = clip;
    time_ = 0.0f;
    pendingEvents_.clear();
}

void Animator::advance(float dt) noexcept {
    if (clip_ == nullptr) return;
    const float duration = clip_->duration;
    if (duration <= 0.0f) return;

    const float oldTime = time_;
    float newTime = oldTime + dt;
    bool wrapped = false;

    if (clip_->looping) {
        if (newTime >= duration || newTime < 0.0f) {
            wrapped = true;
            newTime = std::fmod(newTime, duration);
            if (newTime < 0.0f) newTime += duration;
        }
    } else {
        if (newTime > duration) newTime = duration;
        if (newTime < 0.0f) newTime = 0.0f;
    }

    detail::collectEvents(*clip_, oldTime, newTime, wrapped, pendingEvents_);
    time_ = newTime;
}

void Animator::setTime(float time) noexcept {
    if (clip_ == nullptr) {
        time_ = 0.0f;
        return;
    }
    time_ = detail::wrapTime(time, clip_->duration, clip_->looping);
}

void Animator::samplePose(PoseSpan out) const noexcept {
    if (clip_ == nullptr) return;
    detail::sampleClip(*clip_, time_, out.joints);
}

void Animator::drainEvents(std::vector<EventTrackEvent>& dst) {
    if (pendingEvents_.empty()) return;
    dst.insert(dst.end(), pendingEvents_.begin(), pendingEvents_.end());
    pendingEvents_.clear();
}

} // namespace threadmaxx::animation
