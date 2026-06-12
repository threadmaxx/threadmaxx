/// @file test_animation_animator_stats.cpp
/// @brief A9 — `Animator::stats()` returns an `AnimatorStats` POD
/// describing the current binding (Detached / SingleClip / Graph),
/// playhead, clip duration, graph node count, and pending event
/// queue depth.

#include "Check.hpp"

#include <threadmaxx_animation/eval.hpp>
#include <threadmaxx_animation/graph.hpp>

int main() {
    using namespace threadmaxx::animation;

    // Detached (default-constructed).
    Animator a{};
    auto s = a.stats();
    CHECK(s.mode == AnimatorStats::Mode::Detached);
    CHECK_EQ(s.playheadSeconds, 0.0f);
    CHECK_EQ(s.clipDurationSeconds, 0.0f);
    CHECK_EQ(s.graphNodeCount, 0u);
    CHECK_EQ(s.pendingEventCount, 0u);

    // Single-clip mode.
    ClipDesc clip{};
    clip.name = "walk";
    clip.duration = 2.5f;
    clip.looping = true;
    clip.jointCount = 1;
    clip.keyframeTimes = {0.0f, 1.0f, 2.5f};
    clip.keyframes.resize(3);  // 3 frames * 1 joint = 3 JointPose
    a.setClip(&clip);

    s = a.stats();
    CHECK(s.mode == AnimatorStats::Mode::SingleClip);
    CHECK_EQ(s.playheadSeconds, 0.0f);
    CHECK_EQ(s.clipDurationSeconds, 2.5f);
    CHECK_EQ(s.graphNodeCount, 0u);

    a.advance(1.25f);
    s = a.stats();
    CHECK_EQ(s.playheadSeconds, 1.25f);

    // Switch to graph mode — clip stats clear, graph stats populate.
    AnimationGraph graph{};
    auto clipNode = graph.addClip(&clip, 1.0f);
    auto outNode  = graph.addOutput();
    graph.connect(clipNode, outNode);
    graph.setOutput(outNode);
    a.setGraph(&graph);

    s = a.stats();
    CHECK(s.mode == AnimatorStats::Mode::Graph);
    CHECK_EQ(s.playheadSeconds, 0.0f);
    CHECK_EQ(s.clipDurationSeconds, 0.0f);
    CHECK_EQ(s.graphNodeCount, 2u);

    // Detaching the graph returns to Detached.
    a.setGraph(nullptr);
    s = a.stats();
    CHECK(s.mode == AnimatorStats::Mode::Detached);
    CHECK_EQ(s.graphNodeCount, 0u);

    EXIT_WITH_RESULT();
}
