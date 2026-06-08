#include "Check.hpp"

#include "threadmaxx_physics/constraints.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P6 — `disableCollisionBetweenLinkedBodies` is forwarded verbatim to
// the backend's constraint record. The stub doesn't run a collision
// filter (no real solver), but the flag round-trips through
// `getConstraint` so a real backend (P9) can read it off when wiring
// the constraint into its broadphase filter. We assert the round-trip
// for both `true` and `false`, plus the default (false).

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc box;
    box.type = ShapeType::Box;
    box.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(box);
    ShapeId shapes[1] = {shape};

    BodyDesc bd;
    bd.type = BodyType::Dynamic;
    BodyId bodyA = backend->createBody(world, bd,
                                       std::span<const ShapeId>(shapes, 1));
    BodyId bodyB = backend->createBody(world, bd,
                                       std::span<const ShapeId>(shapes, 1));

    // Default — the flag defaults to false; constraint records that.
    ConstraintDesc defaultDesc;
    defaultDesc.type = ConstraintType::Fixed;
    defaultDesc.bodyA = bodyA;
    defaultDesc.bodyB = bodyB;
    CHECK(!defaultDesc.disableCollisionBetweenLinkedBodies);
    JointId jDefault = createConstraint(*backend, world, defaultDesc);
    auto outDefault = getConstraint(*backend, world, jDefault);
    CHECK(outDefault.has_value());
    CHECK(!outDefault->disableCollisionBetweenLinkedBodies);

    // Opt-in — the flag flows through unchanged.
    ConstraintDesc onDesc = defaultDesc;
    onDesc.disableCollisionBetweenLinkedBodies = true;
    JointId jOn = createConstraint(*backend, world, onDesc);
    auto outOn = getConstraint(*backend, world, jOn);
    CHECK(outOn.has_value());
    CHECK(outOn->disableCollisionBetweenLinkedBodies);

    // Explicit-off — flips back to false.
    ConstraintDesc offDesc = onDesc;
    offDesc.disableCollisionBetweenLinkedBodies = false;
    JointId jOff = createConstraint(*backend, world, offDesc);
    auto outOff = getConstraint(*backend, world, jOff);
    CHECK(outOff.has_value());
    CHECK(!outOff->disableCollisionBetweenLinkedBodies);

    destroyConstraint(*backend, world, jDefault);
    destroyConstraint(*backend, world, jOn);
    destroyConstraint(*backend, world, jOff);
    backend->destroyBody(world, bodyA);
    backend->destroyBody(world, bodyB);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
