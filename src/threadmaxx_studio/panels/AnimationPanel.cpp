/// @file panels/AnimationPanel.cpp
/// @brief ST15 — `AnimationPanel` implementation.

#include <threadmaxx_studio/panels/animation.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_animation/diagnostics.hpp>
#include <threadmaxx_animation/eval.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

const char* modeName(animation::AnimatorStats::Mode m) noexcept {
    switch (m) {
        case animation::AnimatorStats::Mode::Detached:   return "detached";
        case animation::AnimatorStats::Mode::SingleClip: return "single-clip";
        case animation::AnimatorStats::Mode::Graph:      return "graph";
    }
    return "?";
}

} // namespace

AnimationPanel::AnimationPanel(const animation::Animator& animator) noexcept
    : animator_(&animator) {}

void AnimationPanel::render(editor::IEditorBackend& backend,
                            IStudioDataSource&) {
    if (animator_ == nullptr) {
        backend.drawText("Animation: <detached>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }
    const auto s = animator_->stats();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Animation  mode=%s", modeName(s.mode));
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    std::snprintf(buf, sizeof(buf), "playhead=%.3fs  clip=%.3fs",
                  static_cast<double>(s.playheadSeconds),
                  static_cast<double>(s.clipDurationSeconds));
    backend.drawText(buf, 0.0f, y);
    y += 14.0f;

    std::snprintf(buf, sizeof(buf), "graph nodes=%u  events pending=%u",
                  s.graphNodeCount, s.pendingEventCount);
    backend.drawText(buf, 0.0f, y);

    lastRows_ = 3;
}

} // namespace threadmaxx::studio
