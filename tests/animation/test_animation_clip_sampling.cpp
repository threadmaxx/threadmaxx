#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/pose.hpp"

using namespace threadmaxx::animation;

namespace {

constexpr bool nearly(float a, float b, float eps = 1e-5f) noexcept {
    const float d = a - b;
    return (d < eps) && (d > -eps);
}

constexpr bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-5f) noexcept {
    return nearly(a.x, b.x, eps) && nearly(a.y, b.y, eps) && nearly(a.z, b.z, eps);
}

// Three-keyframe 1-second clip on a single-joint rig. The joint
// translates from x=0 → x=10 → x=20 across t=0, 0.5, 1.0.
ClipDesc makeXSweepClip(bool looping) {
    ClipDesc c;
    c.name = "x_sweep";
    c.duration = 1.0f;
    c.looping = looping;
    c.jointCount = 1;
    c.keyframeTimes = {0.0f, 0.5f, 1.0f};
    c.keyframes.resize(3);
    c.keyframes[0].translation = {0.0f, 0.0f, 0.0f};
    c.keyframes[1].translation = {10.0f, 0.0f, 0.0f};
    c.keyframes[2].translation = {20.0f, 0.0f, 0.0f};
    return c;
}

} // namespace

int main() {
    // 1. Sampling at keyframe times reproduces the authored poses.
    {
        ClipDesc clip = makeXSweepClip(false);
        Animator a;
        a.setClip(&clip);
        PoseBuffer buf(1);

        a.setTime(0.0f);
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{0.0f, 0, 0}));

        a.setTime(0.5f);
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{10.0f, 0, 0}));

        a.setTime(1.0f);
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{20.0f, 0, 0}));
    }

    // 2. Linear interp between authored keys.
    {
        ClipDesc clip = makeXSweepClip(false);
        Animator a;
        a.setClip(&clip);
        PoseBuffer buf(1);

        a.setTime(0.25f); // halfway from kf0 to kf1
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{5.0f, 0, 0}));

        a.setTime(0.75f); // halfway from kf1 to kf2
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{15.0f, 0, 0}));
    }

    // 3. Non-looping: time outside [0, duration] clamps.
    {
        ClipDesc clip = makeXSweepClip(false);
        Animator a;
        a.setClip(&clip);
        PoseBuffer buf(1);

        a.setTime(-0.5f);
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{0.0f, 0, 0}));
        CHECK(nearly(a.time(), 0.0f));

        a.setTime(2.0f);
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{20.0f, 0, 0}));
        CHECK(nearly(a.time(), 1.0f));
    }

    // 4. Looping: time outside [0, duration] wraps.
    {
        ClipDesc clip = makeXSweepClip(true);
        Animator a;
        a.setClip(&clip);
        PoseBuffer buf(1);

        a.setTime(1.25f); // wraps to 0.25 → x=5
        CHECK(nearly(a.time(), 0.25f));
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{5.0f, 0, 0}));

        a.setTime(-0.25f); // wraps to 0.75 → x=15
        CHECK(nearly(a.time(), 0.75f));
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{15.0f, 0, 0}));
    }

    // 5. advance() composes with sampling: dt=0.5 from t=0 lands at the
    // middle keyframe's authored pose.
    {
        ClipDesc clip = makeXSweepClip(false);
        Animator a;
        a.setClip(&clip);
        PoseBuffer buf(1);

        a.advance(0.5f);
        a.samplePose(buf.localPose());
        CHECK(nearly(a.time(), 0.5f));
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{10.0f, 0, 0}));
    }

    // 6. setClip(nullptr) detaches and zeros the time; samplePose is
    // a no-op.
    {
        ClipDesc clip = makeXSweepClip(false);
        Animator a;
        a.setClip(&clip);
        a.advance(0.5f);
        CHECK(a.time() == 0.5f);

        a.setClip(nullptr);
        CHECK(a.clip() == nullptr);
        CHECK(a.time() == 0.0f);

        PoseBuffer buf(1);
        buf.localPose().joints[0].translation = {99.0f, 0, 0};
        a.samplePose(buf.localPose()); // no-op
        CHECK(buf.localPose().joints[0].translation.x == 99.0f);
    }

    // 7. Zero-duration clip is sampled safely (degenerates to t=0,
    // returns the first keyframe — no NaN, no divide-by-zero).
    {
        ClipDesc clip;
        clip.duration = 0.0f;
        clip.looping = false;
        clip.jointCount = 1;
        clip.keyframeTimes = {0.0f};
        clip.keyframes.resize(1);
        clip.keyframes[0].translation = {7.0f, 0, 0};

        Animator a;
        a.setClip(&clip);
        a.advance(0.1f);
        PoseBuffer buf(1);
        a.samplePose(buf.localPose());
        CHECK(nearly(buf.localPose().joints[0].translation, Vec3{7.0f, 0, 0}));
    }

    EXIT_WITH_RESULT();
}
