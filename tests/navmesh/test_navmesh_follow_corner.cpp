#include "../Check.hpp"

#include "threadmaxx_navmesh/steering.hpp"

#include <cmath>
#include <vector>

using namespace threadmaxx::navmesh;

// N7 — corridor with a 90° turn. Sim the agent across the turn and
// assert that the per-tick change in velocity direction (the first
// derivative of velocity-direction) stays under the bound implied by
// the acceleration cap. At constant speed, |Δv| ≤ maxAccel*dt forces
// a per-tick angular delta ≤ asin(maxAccel*dt / speed). We use a
// loose factor-of-1.5 safety margin to absorb the transient at the
// corner where speed may be slightly below maxSpeed.

namespace {

float angleBetween(const Vec3& a, const Vec3& b) {
    const float la = std::sqrt(a.x * a.x + a.z * a.z);
    const float lb = std::sqrt(b.x * b.x + b.z * b.z);
    if (la < 1e-6f || lb < 1e-6f) return 0.0f;
    float c = (a.x * b.x + a.z * b.z) / (la * lb);
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return std::acos(c);
}

} // namespace

int main() {
    // L-shaped corridor: +x along z=0, then +z at x=10.
    const std::vector<Vec3> corridor{
        Vec3{0.0f,  0.0f, 0.0f},
        Vec3{10.0f, 0.0f, 0.0f},
        Vec3{10.0f, 0.0f, 10.0f},
    };

    const float kMaxSpeed = 4.0f;
    const float kMaxAccel = 20.0f;
    const float kDt = 1.0f / 60.0f;

    FollowPathInput in;
    in.corridor = corridor;
    in.currentPosition = corridor.front();
    in.currentVelocity = Vec3{0.0f, 0.0f, 0.0f};
    in.maxSpeed = kMaxSpeed;
    in.maxAcceleration = kMaxAccel;
    in.arrivalRadius = 0.1f;
    in.dt = kDt;
    in.segmentIndex = 0;

    // The angular-rate bound implied by the accel cap. For a velocity
    // of magnitude `v`, a delta of magnitude `m = maxAccel*dt` rotates
    // the direction by at most `asin(min(1, m/v))`. Take the bound at
    // maxSpeed for the steady-state assert; for the transient near the
    // corner, allow some headroom because effective speed dips.
    const float steadyStateAngleBound =
        std::asin(std::fmin(1.0f, kMaxAccel * kDt / kMaxSpeed));
    const float angleBudget = steadyStateAngleBound * 1.5f;

    Vec3 pos = in.currentPosition;
    Vec3 vel = in.currentVelocity;
    Vec3 prevDir{};
    bool havePrev = false;
    std::uint32_t cursor = 0;
    bool reachedGoal = false;
    bool sawCornerSegmentSwitch = false;

    // Run long enough to walk a 10+10 m corridor at 4 m/s — call it
    // 10 seconds (~600 ticks) plus headroom for the corner.
    const int kMaxTicks = 1200;
    for (int i = 0; i < kMaxTicks; ++i) {
        FollowPathInput it;
        it.corridor = corridor;
        it.currentPosition = pos;
        it.currentVelocity = vel;
        it.maxSpeed = kMaxSpeed;
        it.maxAcceleration = kMaxAccel;
        it.arrivalRadius = 0.1f;
        it.dt = kDt;
        it.segmentIndex = cursor;
        FollowPathOutput o = followPath(it);

        // Direction-change check kicks in once we have a meaningful
        // previous direction (i.e. we've taken at least one nonzero
        // velocity step). At the very first call from rest we permit
        // any rotation since there's no "previous" direction.
        if (havePrev) {
            const float a = angleBetween(prevDir, o.desiredVelocity);
            CHECK(a <= angleBudget + 1e-4f);
        }
        if (std::sqrt(o.desiredVelocity.x * o.desiredVelocity.x +
                      o.desiredVelocity.z * o.desiredVelocity.z) > 1e-3f) {
            prevDir = o.desiredVelocity;
            havePrev = true;
        }

        if (o.segmentIndex != cursor) sawCornerSegmentSwitch = true;
        cursor = o.segmentIndex;
        vel = o.desiredVelocity;
        pos = Vec3{pos.x + vel.x * kDt, pos.y, pos.z + vel.z * kDt};

        if (o.finished) {
            reachedGoal = true;
            break;
        }
    }

    CHECK(sawCornerSegmentSwitch);
    CHECK(reachedGoal);

    EXIT_WITH_RESULT();
}
