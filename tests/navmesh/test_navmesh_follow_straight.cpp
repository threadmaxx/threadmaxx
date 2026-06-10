#include "../Check.hpp"

#include "threadmaxx_navmesh/steering.hpp"

#include <cmath>
#include <vector>

using namespace threadmaxx::navmesh;

// N7 — agent following a straight corridor produces a velocity along
// the corridor direction at `maxSpeed`. The corridor is a single
// segment along +x; the agent starts at the segment origin with zero
// velocity. After enough accel-clamped ticks the speed should saturate
// at maxSpeed and the direction should be `(1, 0, 0)`.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    const std::vector<Vec3> corridor{
        Vec3{0.0f, 0.0f, 0.0f},
        Vec3{10.0f, 0.0f, 0.0f}
    };

    FollowPathInput in;
    in.corridor = corridor;
    in.currentPosition = Vec3{0.0f, 0.0f, 0.0f};
    in.currentVelocity = Vec3{0.0f, 0.0f, 0.0f};
    in.maxSpeed = 4.0f;
    in.maxAcceleration = 20.0f;
    in.arrivalRadius = 0.05f;
    in.dt = 1.0f / 60.0f;
    in.segmentIndex = 0;

    // First call from rest: desired velocity should aim along +x; the
    // accel cap allows the full step (maxAccel*dt = 20/60 ≈ 0.333,
    // which is the magnitude here since desired = (4, 0, 0)).
    FollowPathOutput firstStep = followPath(in);
    CHECK(!firstStep.finished);
    CHECK(nearly(firstStep.desiredVelocity.z, 0.0f));
    CHECK(firstStep.desiredVelocity.x > 0.0f);
    // |Δv| ≤ maxAccel * dt
    const float maxStep = in.maxAcceleration * in.dt;
    const float dvx = firstStep.desiredVelocity.x - in.currentVelocity.x;
    const float dvz = firstStep.desiredVelocity.z - in.currentVelocity.z;
    CHECK(std::sqrt(dvx * dvx + dvz * dvz) <= maxStep + 1e-4f);

    // Iterate the steering loop to saturation. The agent integrates
    // position with the steered velocity each tick; after a number of
    // ticks well past maxSpeed/maxAccel/dt the speed should match
    // maxSpeed and the direction should still be along +x.
    Vec3 pos = in.currentPosition;
    Vec3 vel = in.currentVelocity;
    std::uint32_t cursor = 0;
    const int kSettleTicks = 100;
    for (int i = 0; i < kSettleTicks; ++i) {
        FollowPathInput it;
        it.corridor = corridor;
        it.currentPosition = pos;
        it.currentVelocity = vel;
        it.maxSpeed = in.maxSpeed;
        it.maxAcceleration = in.maxAcceleration;
        it.arrivalRadius = 0.0f; // disable arrival-snap for the cruise sample
        it.dt = in.dt;
        it.segmentIndex = cursor;
        FollowPathOutput o = followPath(it);
        vel = o.desiredVelocity;
        cursor = o.segmentIndex;
        pos = Vec3{pos.x + vel.x * in.dt,
                   pos.y,
                   pos.z + vel.z * in.dt};
        // Stay well short of the segment end so cruise sampling reads
        // the saturated state — we'll exit early when we've cruised a
        // few seconds.
        if (pos.x >= 5.0f) break;
    }

    const float vSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
    CHECK(nearly(vSpeed, in.maxSpeed));
    CHECK(nearly(vel.z, 0.0f));
    CHECK(vel.x > 0.0f);

    EXIT_WITH_RESULT();
}
