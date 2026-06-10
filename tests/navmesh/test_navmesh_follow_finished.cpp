#include "../Check.hpp"

#include "threadmaxx_navmesh/steering.hpp"

#include <cmath>
#include <vector>

using namespace threadmaxx::navmesh;

// N7 — `finished` flips to true the moment the agent is within
// `arrivalRadius` (XZ distance) of the final waypoint. The check is a
// straight XZ comparison; the segment cursor doesn't have to be on the
// last segment, and the y component is ignored. The output velocity is
// zeroed when finished == true.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    const std::vector<Vec3> corridor{
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{5.0f, 0.0f, 0.0f},
        Vec3{10.0f, 0.0f, 0.0f},
    };

    // Case 1: agent right on top of the goal — finished true, velocity
    // forced to zero.
    {
        FollowPathInput in;
        in.corridor = corridor;
        in.currentPosition = Vec3{10.0f, 0.0f, 0.0f};
        in.currentVelocity = Vec3{4.0f, 0.0f, 0.0f};
        in.maxSpeed = 4.0f;
        in.maxAcceleration = 20.0f;
        in.arrivalRadius = 0.1f;
        in.dt = 1.0f / 60.0f;
        in.segmentIndex = 0;
        FollowPathOutput o = followPath(in);
        CHECK(o.finished);
        CHECK(nearly(o.desiredVelocity.x, 0.0f));
        CHECK(nearly(o.desiredVelocity.y, 0.0f));
        CHECK(nearly(o.desiredVelocity.z, 0.0f));
    }

    // Case 2: agent inside the arrival radius (just under) — finished
    // true; the y axis offset doesn't matter (XZ check only).
    {
        FollowPathInput in;
        in.corridor = corridor;
        in.currentPosition = Vec3{9.95f, 5.0f, 0.03f}; // ~0.058 XZ from goal
        in.currentVelocity = Vec3{4.0f, 0.0f, 0.0f};
        in.maxSpeed = 4.0f;
        in.maxAcceleration = 20.0f;
        in.arrivalRadius = 0.1f;
        in.dt = 1.0f / 60.0f;
        in.segmentIndex = 0;
        FollowPathOutput o = followPath(in);
        CHECK(o.finished);
    }

    // Case 3: agent outside the arrival radius — finished false, the
    // function returns a non-zero steering velocity toward the next
    // segment endpoint.
    {
        FollowPathInput in;
        in.corridor = corridor;
        in.currentPosition = Vec3{9.0f, 0.0f, 0.0f}; // 1.0 m from goal
        in.currentVelocity = Vec3{4.0f, 0.0f, 0.0f};
        in.maxSpeed = 4.0f;
        in.maxAcceleration = 20.0f;
        in.arrivalRadius = 0.1f;
        in.dt = 1.0f / 60.0f;
        in.segmentIndex = 1;
        FollowPathOutput o = followPath(in);
        CHECK(!o.finished);
        const float speed =
            std::sqrt(o.desiredVelocity.x * o.desiredVelocity.x +
                      o.desiredVelocity.z * o.desiredVelocity.z);
        CHECK(speed > 0.0f);
    }

    // Case 4: degenerate corridor (single waypoint) reports finished
    // unconditionally — there's nothing to follow.
    {
        const std::vector<Vec3> single{Vec3{0.0f, 0.0f, 0.0f}};
        FollowPathInput in;
        in.corridor = single;
        in.currentPosition = Vec3{99.0f, 0.0f, 99.0f};
        in.dt = 1.0f / 60.0f;
        FollowPathOutput o = followPath(in);
        CHECK(o.finished);
    }

    EXIT_WITH_RESULT();
}
