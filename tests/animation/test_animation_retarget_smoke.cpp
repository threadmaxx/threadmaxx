#include "Check.hpp"
#include "threadmaxx_animation/pose.hpp"
#include "threadmaxx_animation/retarget.hpp"
#include "threadmaxx_animation/skeleton.hpp"

#include <cstddef>
#include <vector>

using namespace threadmaxx::animation;

namespace {

SkeletonDesc makeRig(const char* name,
                     std::initializer_list<const char*> jointNames) {
    SkeletonDesc s;
    s.name = name;
    int parent = -1;
    for (const char* jn : jointNames) {
        Joint j;
        j.name = jn;
        j.parent = parent;
        j.bindLocal = JointPose{{0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}};
        s.joints.push_back(std::move(j));
        parent = static_cast<int>(s.joints.size()) - 1;
    }
    return s;
}

bool nearly(const Quat& a, const Quat& b, float eps = 1e-6f) {
    return std::abs(a.x - b.x) < eps
        && std::abs(a.y - b.y) < eps
        && std::abs(a.z - b.z) < eps
        && std::abs(a.w - b.w) < eps;
}

bool nearly(const Vec3& a, const Vec3& b, float eps = 1e-6f) {
    return std::abs(a.x - b.x) < eps
        && std::abs(a.y - b.y) < eps
        && std::abs(a.z - b.z) < eps;
}

} // namespace

int main() {
    // === 1. Same skeleton on both sides — every joint maps 1:1, and
    // rotations carry across.
    {
        SkeletonDesc rig = makeRig("rigA", {"root", "spine", "head"});
        RetargetMap map = buildRetargetMap(rig, rig);
        CHECK_EQ(map.size(), std::size_t{3});
        CHECK_EQ(map.sourceName, std::string{"rigA"});
        CHECK_EQ(map.destName, std::string{"rigA"});

        std::vector<JointPose> srcPose(3);
        srcPose[0].rotation = Quat{0, 0, 0.5f, 0.866f};  // ~60° about z.
        srcPose[1].rotation = Quat{0.1f, 0, 0, 0.995f};
        srcPose[2].rotation = Quat{0, 0.2f, 0, 0.98f};

        std::vector<JointPose> dstPose(3);
        retargetPose(srcPose, std::span<JointPose>(dstPose), map);

        CHECK(nearly(dstPose[0].rotation, srcPose[0].rotation));
        CHECK(nearly(dstPose[1].rotation, srcPose[1].rotation));
        CHECK(nearly(dstPose[2].rotation, srcPose[2].rotation));
    }

    // === 2. Subset rig — dest has fewer joints. Mapping only covers
    // names present in both; unmapped src joints don't crash.
    {
        SkeletonDesc src = makeRig("full",   {"root", "spine", "neck", "head"});
        SkeletonDesc dst = makeRig("simple", {"root", "head"});
        RetargetMap map = buildRetargetMap(src, dst);

        // Two joint names present in both: "root", "head". 2 mappings.
        CHECK_EQ(map.size(), std::size_t{2});
    }

    // === 3. Order divergence — dst has the same names, different
    // indices. Each mapping respects the actual indices.
    {
        SkeletonDesc src = makeRig("A", {"root", "spine", "head"});
        SkeletonDesc dst = makeRig("B", {"head", "spine", "root"});
        RetargetMap map = buildRetargetMap(src, dst);
        CHECK_EQ(map.size(), std::size_t{3});

        std::vector<JointPose> srcPose(3);
        srcPose[0].rotation = Quat{1, 0, 0, 0};  // root marker.
        srcPose[1].rotation = Quat{0, 1, 0, 0};  // spine marker.
        srcPose[2].rotation = Quat{0, 0, 1, 0};  // head marker.

        std::vector<JointPose> dstPose(3);
        retargetPose(srcPose, std::span<JointPose>(dstPose), map);

        // dst indices 0/1/2 = "head"/"spine"/"root" → marker quats land
        // at dst[2]/dst[1]/dst[0].
        CHECK(nearly(dstPose[2].rotation, Quat{1, 0, 0, 0}));
        CHECK(nearly(dstPose[1].rotation, Quat{0, 1, 0, 0}));
        CHECK(nearly(dstPose[0].rotation, Quat{0, 0, 1, 0}));
    }

    // === 4. Unmapped dest joints retain caller-seeded values.
    {
        SkeletonDesc src = makeRig("A", {"root", "head"});
        SkeletonDesc dst = makeRig("B", {"root", "spine", "head"});
        RetargetMap map = buildRetargetMap(src, dst);
        CHECK_EQ(map.size(), std::size_t{2});  // root, head only.

        std::vector<JointPose> srcPose(2);
        srcPose[0].rotation = Quat{0, 0, 0, 1};
        srcPose[1].rotation = Quat{0.5f, 0, 0, 0.866f};

        std::vector<JointPose> dstPose(3);
        const Quat sentinel{0.123f, 0.456f, 0.789f, 0.321f};
        dstPose[1].rotation = sentinel;  // spine — unmapped.

        retargetPose(srcPose, std::span<JointPose>(dstPose), map);

        CHECK(nearly(dstPose[1].rotation, sentinel));  // preserved.
        CHECK(nearly(dstPose[2].rotation, srcPose[1].rotation));
    }

    // === 5. Channel selection — translation off by default; turning
    // it on copies; scale stays off unless requested.
    {
        SkeletonDesc rig = makeRig("R", {"root", "head"});
        RetargetMap map = buildRetargetMap(rig, rig);
        std::vector<JointPose> srcPose(2);
        srcPose[0].translation = {1, 2, 3};
        srcPose[0].scale = {2, 2, 2};
        srcPose[1].translation = {4, 5, 6};

        // Default channels: rotation only.
        {
            std::vector<JointPose> dstPose(2);
            retargetPose(srcPose, std::span<JointPose>(dstPose), map);
            CHECK(nearly(dstPose[0].translation, Vec3{0, 0, 0}));
            CHECK(nearly(dstPose[0].scale, Vec3{1, 1, 1}));
        }

        // copyTranslation = true.
        {
            RetargetChannels ch;
            ch.copyTranslation = true;
            std::vector<JointPose> dstPose(2);
            retargetPose(srcPose, std::span<JointPose>(dstPose), map, ch);
            CHECK(nearly(dstPose[0].translation, Vec3{1, 2, 3}));
            CHECK(nearly(dstPose[1].translation, Vec3{4, 5, 6}));
            CHECK(nearly(dstPose[0].scale, Vec3{1, 1, 1}));
        }

        // copyScale = true.
        {
            RetargetChannels ch;
            ch.copyScale = true;
            ch.copyRotation = false;
            std::vector<JointPose> dstPose(2);
            retargetPose(srcPose, std::span<JointPose>(dstPose), map, ch);
            CHECK(nearly(dstPose[0].scale, Vec3{2, 2, 2}));
        }
    }

    // === 6. Out-of-range index in a stale map is a no-op (no UB).
    {
        std::vector<JointPose> srcPose(2);
        std::vector<JointPose> dstPose(2);
        RetargetMap stale;
        stale.mappings.push_back(RetargetJointMapping{99, 0});
        stale.mappings.push_back(RetargetJointMapping{0, 99});
        // Neither mapping should apply. dstPose stays at defaults.
        retargetPose(srcPose, std::span<JointPose>(dstPose), stale);
        CHECK(nearly(dstPose[0].rotation, Quat{0, 0, 0, 1}));
    }

    // === 7. PoseSpan overload — same semantics.
    {
        SkeletonDesc rig = makeRig("R", {"root", "head"});
        RetargetMap map = buildRetargetMap(rig, rig);
        std::vector<JointPose> srcPose(2);
        srcPose[1].rotation = Quat{0, 0, 0.5f, 0.866f};

        PoseBuffer buf;
        buf.resize(2);
        retargetPose(srcPose, buf.localPose(), map);
        CHECK(nearly(buf.localPose().joints[1].rotation,
                     srcPose[1].rotation));
    }

    EXIT_WITH_RESULT();
}
