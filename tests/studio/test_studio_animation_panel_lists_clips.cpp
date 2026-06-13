/// @file test_studio_animation_panel_lists_clips.cpp
/// @brief ST15 — AnimationPanel reads the bound Animator's A9 stats
/// and renders mode/playhead/clip rows.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/animation.hpp>

#include <threadmaxx_animation/clip.hpp>
#include <threadmaxx_animation/eval.hpp>
#include <threadmaxx_animation/pose.hpp>

namespace {

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::AnimationPanel panel;

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached — placeholder.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Bind an animator in single-clip mode.
    animation::ClipDesc clip;
    clip.name = "Idle";
    clip.duration = 2.5f;
    clip.jointCount = 1;
    clip.keyframeTimes = {0.0f, 2.5f};
    clip.keyframes.resize(2);
    animation::Animator animator;
    animator.setClip(&clip);
    animator.advance(1.0f);
    panel.setAnimator(&animator);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 3u);
    // beginFrame() clears the captured op log; this frame holds 3 ops.
    CHECK_EQ(countTextOps(backend.capturedFrame()), 3u);

    // Detach via setAnimator(nullptr) → placeholder again.
    panel.setAnimator(nullptr);
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
