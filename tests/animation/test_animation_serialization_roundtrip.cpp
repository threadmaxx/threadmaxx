#include "Check.hpp"
#include "threadmaxx_animation/clip.hpp"
#include "threadmaxx_animation/serialization.hpp"
#include "threadmaxx_animation/skeleton.hpp"

#include <cstring>
#include <vector>

using namespace threadmaxx::animation;

namespace {

SkeletonDesc makeFullSkeleton() {
    SkeletonDesc s;
    s.name = "humanoid";
    s.joints = {
        Joint{"root",  -1,
              JointPose{{0, 0, 0},      {0, 0, 0, 1}, {1, 1, 1}}},
        Joint{"spine", 0,
              JointPose{{0, 1, 0},      {0, 0, 0, 1}, {1, 1, 1}}},
        Joint{"head",  1,
              JointPose{{0, 0.5f, 0},   {0, 0.1f, 0, 0.995f}, {1, 1, 1}}},
    };
    s.bindGlobal = {
        JointPose{{0, 0, 0},    {0, 0, 0, 1}, {1, 1, 1}},
        JointPose{{0, 1, 0},    {0, 0, 0, 1}, {1, 1, 1}},
        JointPose{{0, 1.5f, 0}, {0, 0, 0, 1}, {1, 1, 1}},
    };
    return s;
}

ClipDesc makeFullClip() {
    ClipDesc c;
    c.name = "walk_cycle";
    c.duration = 1.5f;
    c.looping = true;
    c.events = {
        EventTrackEvent{0.0f,   "foot_left_down"},
        EventTrackEvent{0.75f,  "foot_right_down"},
    };
    c.jointCount = 3;
    c.keyframeTimes = {0.0f, 0.5f, 1.0f, 1.5f};
    c.keyframes.reserve(c.keyframeTimes.size() * c.jointCount);
    for (std::size_t k = 0; k < c.keyframeTimes.size(); ++k) {
        for (std::size_t j = 0; j < c.jointCount; ++j) {
            JointPose p;
            p.translation = {static_cast<float>(k),
                             static_cast<float>(j),
                             static_cast<float>(k + j)};
            p.rotation = {0, 0, 0, 1};
            p.scale = {1, 1, 1};
            c.keyframes.push_back(p);
        }
    }
    return c;
}

bool poseEq(const JointPose& a, const JointPose& b) {
    return std::memcmp(&a, &b, sizeof(JointPose)) == 0;
}

bool jointEq(const Joint& a, const Joint& b) {
    return a.name == b.name
        && a.parent == b.parent
        && poseEq(a.bindLocal, b.bindLocal);
}

bool skeletonEq(const SkeletonDesc& a, const SkeletonDesc& b) {
    if (a.name != b.name) return false;
    if (a.joints.size() != b.joints.size()) return false;
    for (std::size_t i = 0; i < a.joints.size(); ++i) {
        if (!jointEq(a.joints[i], b.joints[i])) return false;
    }
    if (a.bindGlobal.size() != b.bindGlobal.size()) return false;
    for (std::size_t i = 0; i < a.bindGlobal.size(); ++i) {
        if (!poseEq(a.bindGlobal[i], b.bindGlobal[i])) return false;
    }
    return true;
}

bool clipEq(const ClipDesc& a, const ClipDesc& b) {
    if (a.name != b.name) return false;
    if (a.duration != b.duration) return false;
    if (a.looping != b.looping) return false;
    if (a.events.size() != b.events.size()) return false;
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        if (a.events[i].time != b.events[i].time) return false;
        if (a.events[i].name != b.events[i].name) return false;
    }
    if (a.jointCount != b.jointCount) return false;
    if (a.keyframeTimes != b.keyframeTimes) return false;
    if (a.keyframes.size() != b.keyframes.size()) return false;
    for (std::size_t i = 0; i < a.keyframes.size(); ++i) {
        if (!poseEq(a.keyframes[i], b.keyframes[i])) return false;
    }
    return true;
}

} // namespace

int main() {
    // === 1. Round-trip a single skeleton + single clip bundle.
    {
        AnimationAssetBundle in;
        in.skeletons.push_back(makeFullSkeleton());
        in.clips.push_back(makeFullClip());

        std::vector<std::uint8_t> bytes = writeAnimationAssetBundle(in);
        CHECK(!bytes.empty());

        auto out = readAnimationAssetBundle(bytes);
        CHECK(out.has_value());
        CHECK_EQ(out->skeletons.size(), std::size_t{1});
        CHECK_EQ(out->clips.size(), std::size_t{1});
        CHECK(skeletonEq(in.skeletons[0], out->skeletons[0]));
        CHECK(clipEq(in.clips[0], out->clips[0]));
    }

    // === 2. Empty bundle round-trips (header-only stream).
    {
        AnimationAssetBundle in;
        auto bytes = writeAnimationAssetBundle(in);
        auto out = readAnimationAssetBundle(bytes);
        CHECK(out.has_value());
        CHECK(out->skeletons.empty());
        CHECK(out->clips.empty());
    }

    // === 3. Multiple skeletons + clips round-trip.
    {
        AnimationAssetBundle in;
        in.skeletons.push_back(makeFullSkeleton());
        SkeletonDesc s2 = makeFullSkeleton();
        s2.name = "humanoid_lite";
        s2.joints.pop_back();
        s2.bindGlobal.pop_back();
        in.skeletons.push_back(std::move(s2));

        in.clips.push_back(makeFullClip());
        ClipDesc c2 = makeFullClip();
        c2.name = "run_cycle";
        c2.duration = 0.5f;
        c2.looping = false;
        in.clips.push_back(std::move(c2));

        auto bytes = writeAnimationAssetBundle(in);
        auto out = readAnimationAssetBundle(bytes);
        CHECK(out.has_value());
        CHECK_EQ(out->skeletons.size(), std::size_t{2});
        CHECK_EQ(out->clips.size(), std::size_t{2});
        CHECK(skeletonEq(in.skeletons[0], out->skeletons[0]));
        CHECK(skeletonEq(in.skeletons[1], out->skeletons[1]));
        CHECK(clipEq(in.clips[0], out->clips[0]));
        CHECK(clipEq(in.clips[1], out->clips[1]));
    }

    // === 4. Bad magic → reader returns nullopt.
    {
        std::vector<std::uint8_t> bytes(64, 0);
        auto out = readAnimationAssetBundle(bytes);
        CHECK(!out.has_value());
    }

    // === 5. Bad version → reader returns nullopt.
    {
        AnimationAssetBundle in;
        auto bytes = writeAnimationAssetBundle(in);
        // Stomp the version u32 (immediately after the 4-byte magic).
        CHECK(bytes.size() >= 8);
        bytes[4] = 0xff;
        bytes[5] = 0xff;
        bytes[6] = 0xff;
        bytes[7] = 0xff;
        auto out = readAnimationAssetBundle(bytes);
        CHECK(!out.has_value());
    }

    // === 6. Truncated stream → reader returns nullopt.
    {
        AnimationAssetBundle in;
        in.skeletons.push_back(makeFullSkeleton());
        in.clips.push_back(makeFullClip());
        auto bytes = writeAnimationAssetBundle(in);
        CHECK(bytes.size() > 64);
        bytes.resize(bytes.size() / 2);
        auto out = readAnimationAssetBundle(bytes);
        CHECK(!out.has_value());
    }

    // === 7. Determinism — same input produces byte-identical bytes.
    {
        AnimationAssetBundle in;
        in.skeletons.push_back(makeFullSkeleton());
        in.clips.push_back(makeFullClip());
        auto a = writeAnimationAssetBundle(in);
        auto b = writeAnimationAssetBundle(in);
        CHECK_EQ(a.size(), b.size());
        CHECK(std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    EXIT_WITH_RESULT();
}
