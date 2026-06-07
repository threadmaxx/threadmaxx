#include "Check.hpp"
#include "threadmaxx_animation/registry.hpp"
#include "threadmaxx_animation/skeleton.hpp"

using namespace threadmaxx::animation;

int main() {
    AnimationRegistry reg;

    SkeletonDesc desc;
    desc.name = "humanoid_3";
    desc.joints = {
        Joint{"root",  -1, JointPose{{0.0f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}}},
        Joint{"spine",  0, JointPose{{0.0f, 1.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}}},
        Joint{"head",   1, JointPose{{0.0f, 0.5f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}}},
    };
    desc.bindGlobal = {
        JointPose{{0.0f, 0.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}},
        JointPose{{0.0f, 1.0f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}},
        JointPose{{0.0f, 1.5f, 0.0f}, {0, 0, 0, 1}, {1, 1, 1}},
    };

    SkeletonRef ref = reg.addSkeleton(desc);
    CHECK(reg.isValid(ref));

    const SkeletonDesc* got = reg.getSkeleton(ref);
    CHECK(got != nullptr);
    CHECK_EQ(got->name, std::string{"humanoid_3"});
    CHECK_EQ(got->joints.size(), std::size_t{3});
    CHECK_EQ(got->joints[0].name, std::string{"root"});
    CHECK_EQ(got->joints[1].parent, 0);
    CHECK_EQ(got->joints[2].parent, 1);
    CHECK(got->joints[1].bindLocal.translation.y == 1.0f);
    CHECK_EQ(got->bindGlobal.size(), std::size_t{3});
    CHECK(got->bindGlobal[2].translation.y == 1.5f);

    // A stale ref (wrong generation) must be rejected without UB.
    SkeletonRef stale = ref;
    stale.generation = 999;
    CHECK(!reg.isValid(stale));
    CHECK(reg.getSkeleton(stale) == nullptr);

    // A ref with out-of-range id is also rejected.
    SkeletonRef bogus;
    bogus.id = 9999;
    bogus.generation = 1;
    CHECK(!reg.isValid(bogus));
    CHECK(reg.getSkeleton(bogus) == nullptr);

    EXIT_WITH_RESULT();
}
