#include "Check.hpp"

#include "threadmaxx_physics/jolt_backend.hpp"
#include "threadmaxx_physics/threadmaxx_physics.hpp"

#include <cmath>
#include <vector>

// P9 — Jolt backend conformance subset.
//
// Re-runs a subset of the P1-P8 invariants against the Jolt backend
// instead of the Stub. Tolerances are documented per-check because Jolt
// is a real solver: its kinematic integration uses substeps, its
// queries follow OBB / capsule narrowphase, and constraint solving is
// iterative. Bit-equality with the Stub is NOT the gate; behavioral
// equivalence (within stated tolerances) is.
//
// Subset coverage:
//   - P3 body lifecycle: create, getBodyState, destroyBody.
//   - P3 kinematic teleport: setBodyTransform takes effect immediately.
//   - P4 stepping: kinematic body with linearVelocity advances roughly
//     by velocity * total_dt over many short steps (Jolt's
//     `EMotionType::Kinematic` integrates ballistically just like the
//     Stub does).
//   - P5 queries: raycast hits a body in its path; raycast miss returns
//     nullopt; overlap query at a body's center returns that body.
//   - P6 constraints: createConstraint / getConstraint round-trip;
//     destroyConstraint removes the descriptor.
//   - P8 contact events: two bodies forced into AABB overlap fire at
//     least one Begin event.
//
// Tolerances:
//   - Position: |dx| <= 0.05 m (5 cm) for kinematic predictions.
//   - Raycast distance: ±0.05 m of geometric expectation.

using namespace threadmaxx::physics;

namespace {

bool approxEqual(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

bool approxEqual(const Vec3& a, const Vec3& b, float tol) {
    return approxEqual(a.x, b.x, tol)
        && approxEqual(a.y, b.y, tol)
        && approxEqual(a.z, b.z, tol);
}

} // namespace

int main() {
    auto backend = makeJoltBackend();
    CHECK(backend != nullptr);
    if (!backend) {
        EXIT_WITH_RESULT();
    }

    PhysicsConfig cfg;
    cfg.fixedTimestep = 1.0f / 60.0f;
    cfg.maxSubSteps = 1;
    cfg.allowSolverThreading = false;
    PhysicsWorldId world = backend->createWorld(cfg);
    CHECK(world.value != 0);

    ShapeDesc boxDesc;
    boxDesc.type = ShapeType::Box;
    boxDesc.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId boxShape = backend->createShape(boxDesc);
    CHECK(boxShape.value != 0);
    ShapeId boxShapes[1] = {boxShape};

    // --- P3: body lifecycle ---
    BodyDesc descA;
    descA.type = BodyType::Kinematic;
    descA.position = Vec3{0.0f, 0.0f, 0.0f};
    BodyId bodyA = backend->createBody(world, descA,
                                       std::span<const ShapeId>(boxShapes, 1));
    CHECK(bodyA.value != 0);
    auto stateA = backend->getBodyState(world, bodyA);
    CHECK(stateA.has_value());
    CHECK(approxEqual(stateA->position, Vec3{0.0f, 0.0f, 0.0f}, 1e-4f));

    // --- P3: kinematic teleport ---
    backend->setBodyTransform(world, bodyA, Vec3{5.0f, 0.0f, 0.0f}, Quat{});
    stateA = backend->getBodyState(world, bodyA);
    CHECK(stateA.has_value());
    CHECK(approxEqual(stateA->position, Vec3{5.0f, 0.0f, 0.0f}, 1e-4f));

    // --- P4: stepping a kinematic body with linear velocity ---
    BodyDesc descMover;
    descMover.type = BodyType::Kinematic;
    descMover.position = Vec3{0.0f, 5.0f, 0.0f};
    descMover.linearVelocity = Vec3{1.0f, 0.0f, 0.0f};
    BodyId mover = backend->createBody(world, descMover,
                                       std::span<const ShapeId>(boxShapes, 1));
    CHECK(mover.value != 0);
    // Step 60 ticks at dt = 1/60 → expect ~1 m of travel along +X.
    for (int i = 0; i < 60; ++i) backend->stepWorld(world, cfg.fixedTimestep);
    auto stateMover = backend->getBodyState(world, mover);
    CHECK(stateMover.has_value());
    CHECK(approxEqual(stateMover->position.x, 1.0f, 0.05f));

    // --- P5: raycast hit ---
    // Put the target in a clear lane at y=20 — the mover kinematic
    // from the previous step lives at y=5 and would otherwise shadow
    // the test ray.
    BodyDesc descTarget;
    descTarget.type = BodyType::Static;
    descTarget.position = Vec3{10.0f, 20.0f, 0.0f};
    BodyId target = backend->createBody(world, descTarget,
                                        std::span<const ShapeId>(boxShapes, 1));
    CHECK(target.value != 0);
    RaycastRequest ray;
    ray.origin = Vec3{-5.0f, 20.0f, 0.0f};
    ray.direction = Vec3{1.0f, 0.0f, 0.0f};
    ray.maxDistance = 100.0f;
    auto hit = backend->raycast(world, ray);
    CHECK(hit.has_value());
    if (hit.has_value()) {
        // Front face of the half-extent-0.5 box at x=10 sits at x=9.5;
        // ray origin x=-5 so the geometric distance is 14.5.
        CHECK(approxEqual(hit->distance, 14.5f, 0.05f));
        CHECK(hit->body == target);
    }

    // --- P5: raycast miss ---
    RaycastRequest skyRay;
    skyRay.origin = Vec3{0.0f, 100.0f, 0.0f};
    skyRay.direction = Vec3{0.0f, 1.0f, 0.0f};
    skyRay.maxDistance = 10.0f;
    auto sky = backend->raycast(world, skyRay);
    CHECK(!sky.has_value());

    // --- P5: overlap ---
    OverlapRequest overReq;
    overReq.center = Vec3{10.0f, 20.0f, 0.0f};
    overReq.radius = 0.25f;
    std::vector<BodyId> overlapHits;
    backend->overlap(world, overReq, overlapHits);
    bool foundTarget = false;
    for (BodyId b : overlapHits) {
        if (b == target) { foundTarget = true; break; }
    }
    CHECK(foundTarget);

    // --- P6: constraint round-trip ---
    BodyDesc descAnchor;
    descAnchor.type = BodyType::Static;
    descAnchor.position = Vec3{-20.0f, 0.0f, 0.0f};
    BodyId anchor = backend->createBody(world, descAnchor,
                                        std::span<const ShapeId>(boxShapes, 1));

    BodyDesc descSwing;
    descSwing.type = BodyType::Dynamic;
    descSwing.position = Vec3{-19.0f, 0.0f, 0.0f};
    BodyId swing = backend->createBody(world, descSwing,
                                       std::span<const ShapeId>(boxShapes, 1));

    ConstraintDesc hinge;
    hinge.type = ConstraintType::Hinge;
    hinge.bodyA = anchor;
    hinge.bodyB = swing;
    hinge.localAxisA = Vec3{0.0f, 1.0f, 0.0f};
    hinge.localAxisB = Vec3{0.0f, 1.0f, 0.0f};
    hinge.disableCollisionBetweenLinkedBodies = true;
    JointId joint = backend->createConstraint(world, hinge);
    CHECK(joint.value != 0);
    auto rt = backend->getConstraint(world, joint);
    CHECK(rt.has_value());
    if (rt.has_value()) {
        CHECK(rt->type == ConstraintType::Hinge);
        CHECK(rt->bodyA == anchor);
        CHECK(rt->bodyB == swing);
        CHECK(rt->disableCollisionBetweenLinkedBodies);
    }
    backend->destroyConstraint(world, joint);
    CHECK(!backend->getConstraint(world, joint).has_value());

    // --- P8: contact events ---
    std::vector<ContactEvent> events;
    backend->setContactCallback(world,
                                [&events](const ContactEvent& ev) {
                                    events.push_back(ev);
                                });

    // Two new dynamic boxes initially apart, then teleported into overlap.
    BodyDesc descX;
    descX.type = BodyType::Dynamic;
    descX.position = Vec3{30.0f, 0.0f, 0.0f};
    BodyId bX = backend->createBody(world, descX,
                                    std::span<const ShapeId>(boxShapes, 1));
    BodyDesc descY = descX;
    descY.position = Vec3{40.0f, 0.0f, 0.0f};
    BodyId bY = backend->createBody(world, descY,
                                    std::span<const ShapeId>(boxShapes, 1));
    // Settle.
    for (int i = 0; i < 2; ++i) backend->stepWorld(world, cfg.fixedTimestep);
    events.clear();
    // Move bY on top of bX.
    backend->setBodyTransform(world, bY, Vec3{30.0f, 0.0f, 0.0f}, Quat{});
    for (int i = 0; i < 4; ++i) backend->stepWorld(world, cfg.fixedTimestep);
    bool gotBegin = false;
    for (const auto& ev : events) {
        if (ev.phase == ContactPhase::Begin) { gotBegin = true; break; }
    }
    CHECK(gotBegin);

    // Teardown.
    backend->destroyBody(world, bX);
    backend->destroyBody(world, bY);
    backend->destroyBody(world, swing);
    backend->destroyBody(world, anchor);
    backend->destroyBody(world, target);
    backend->destroyBody(world, mover);
    backend->destroyBody(world, bodyA);
    backend->destroyShape(boxShape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
