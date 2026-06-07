#include "Check.hpp"
#include "threadmaxx_animation/pose.hpp"

using namespace threadmaxx::animation;

int main() {
    PoseBuffer a(4);
    CHECK_EQ(a.size(), std::size_t{4});
    CHECK_EQ(a.localPose().joints.size(), std::size_t{4});

    a.resize(7);
    CHECK_EQ(a.size(), std::size_t{7});
    CHECK_EQ(a.localPose().joints.size(), std::size_t{7});

    // Default JointPose has scale = (1,1,1) and identity rotation.
    PoseBuffer fresh(3);
    auto span = fresh.localPose().joints;
    for (std::size_t i = 0; i < span.size(); ++i) {
        CHECK(span[i].scale.x == 1.0f);
        CHECK(span[i].scale.y == 1.0f);
        CHECK(span[i].scale.z == 1.0f);
        CHECK(span[i].rotation.w == 1.0f);
    }

    // Two buffers must not alias each other: writing to one leaves
    // the other untouched. This is the assert that catches an
    // accidental shared-storage refactor.
    PoseBuffer b(2);
    PoseBuffer c(2);
    b.localPose().joints[0].translation = Vec3{42.0f, 0.0f, 0.0f};
    CHECK(b.localPose().joints[0].translation.x == 42.0f);
    CHECK(c.localPose().joints[0].translation.x == 0.0f);

    // Empty buffer is handled.
    PoseBuffer empty;
    CHECK_EQ(empty.size(), std::size_t{0});
    CHECK(empty.empty());
    CHECK(empty.localPose().joints.empty());

    EXIT_WITH_RESULT();
}
