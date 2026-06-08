#include "Check.hpp"

#include "threadmaxx_physics/constraints.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P6 — create a hinge constraint between two bodies; the descriptor
// round-trips through `getConstraint` byte-for-byte. Also verifies the
// guard rails: zero-id world / stale body / self-constraint all return
// a zero `JointId`.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc box;
    box.type = ShapeType::Box;
    box.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(box);

    BodyDesc a;
    a.type = BodyType::Dynamic;
    a.position = Vec3{0.0f, 0.0f, 0.0f};
    ShapeId shapes[1] = {shape};
    BodyId bodyA = backend->createBody(world, a,
                                       std::span<const ShapeId>(shapes, 1));

    BodyDesc b;
    b.type = BodyType::Dynamic;
    b.position = Vec3{2.0f, 0.0f, 0.0f};
    BodyId bodyB = backend->createBody(world, b,
                                       std::span<const ShapeId>(shapes, 1));

    ConstraintDesc desc;
    desc.type = ConstraintType::Hinge;
    desc.bodyA = bodyA;
    desc.bodyB = bodyB;
    desc.localAnchorA = Vec3{0.5f, 0.0f, 0.0f};
    desc.localAnchorB = Vec3{-0.5f, 0.0f, 0.0f};
    desc.localAxisA = Vec3{0.0f, 1.0f, 0.0f};
    desc.localAxisB = Vec3{0.0f, 1.0f, 0.0f};
    // Hinge reads `angularLimits[0]` — set a ±45° wedge.
    desc.angularLimits[0].min = -0.785398f;
    desc.angularLimits[0].max =  0.785398f;
    desc.disableCollisionBetweenLinkedBodies = true;

    JointId joint = createConstraint(*backend, world, desc);
    CHECK(joint.value != 0u);

    auto out = getConstraint(*backend, world, joint);
    CHECK(out.has_value());
    CHECK(out->type == ConstraintType::Hinge);
    CHECK(out->bodyA == bodyA);
    CHECK(out->bodyB == bodyB);
    CHECK(out->localAnchorA.x == 0.5f);
    CHECK(out->localAnchorB.x == -0.5f);
    CHECK(out->localAxisA.y == 1.0f);
    CHECK(out->angularLimits[0].min == -0.785398f);
    CHECK(out->angularLimits[0].max ==  0.785398f);
    CHECK(out->disableCollisionBetweenLinkedBodies);

    // Guard rails: invalid world returns zero id, lookup returns nullopt.
    JointId bad = createConstraint(*backend, PhysicsWorldId{}, desc);
    CHECK(bad.value == 0u);
    CHECK(!getConstraint(*backend, PhysicsWorldId{}, joint).has_value());

    // Stale body — destroy A then attempt a fresh constraint referencing it.
    BodyId staleA = bodyA;
    backend->destroyBody(world, bodyA);
    ConstraintDesc stale = desc;
    stale.bodyA = staleA;
    // Need a fresh B since destroying A also invalidated the original
    // constraint (covered by the destroy test); make a third body.
    BodyDesc c;
    c.type = BodyType::Dynamic;
    BodyId bodyC = backend->createBody(world, c,
                                       std::span<const ShapeId>(shapes, 1));
    stale.bodyB = bodyC;
    CHECK(createConstraint(*backend, world, stale).value == 0u);

    // Self-constraint: bodyB ↔ bodyB.
    ConstraintDesc self;
    self.type = ConstraintType::Fixed;
    self.bodyA = bodyB;
    self.bodyB = bodyB;
    CHECK(createConstraint(*backend, world, self).value == 0u);

    backend->destroyBody(world, bodyB);
    backend->destroyBody(world, bodyC);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
