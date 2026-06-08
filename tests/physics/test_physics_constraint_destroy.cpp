#include "Check.hpp"

#include "threadmaxx_physics/constraints.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P6 — destroying either body of a constraint invalidates the joint
// without crashing. `getConstraint` returns nullopt, and an explicit
// `destroyConstraint` on the stale id is a no-op. Also exercises the
// happy-path explicit destroy + stale-id idempotency.

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

    // Helper: create a fresh A–B pair + a fixed constraint between them.
    auto makePair = [&]() -> std::tuple<BodyId, BodyId, JointId> {
        BodyDesc bd;
        bd.type = BodyType::Dynamic;
        bd.position = Vec3{0.0f, 0.0f, 0.0f};
        BodyId a = backend->createBody(world, bd,
                                       std::span<const ShapeId>(shapes, 1));
        bd.position = Vec3{2.0f, 0.0f, 0.0f};
        BodyId b = backend->createBody(world, bd,
                                       std::span<const ShapeId>(shapes, 1));
        ConstraintDesc desc;
        desc.type = ConstraintType::Fixed;
        desc.bodyA = a;
        desc.bodyB = b;
        JointId j = createConstraint(*backend, world, desc);
        CHECK(j.value != 0u);
        CHECK(getConstraint(*backend, world, j).has_value());
        return {a, b, j};
    };

    // Case 1: destroying bodyA invalidates the constraint.
    {
        auto [a, b, joint] = makePair();
        backend->destroyBody(world, a);
        CHECK(!getConstraint(*backend, world, joint).has_value());
        // No-op double-destroy on the stale joint must not crash.
        destroyConstraint(*backend, world, joint);
        CHECK(!getConstraint(*backend, world, joint).has_value());
        backend->destroyBody(world, b);
    }

    // Case 2: destroying bodyB invalidates the constraint.
    {
        auto [a, b, joint] = makePair();
        backend->destroyBody(world, b);
        CHECK(!getConstraint(*backend, world, joint).has_value());
        backend->destroyBody(world, a);
    }

    // Case 3: happy-path explicit destroy.
    {
        auto [a, b, joint] = makePair();
        destroyConstraint(*backend, world, joint);
        CHECK(!getConstraint(*backend, world, joint).has_value());
        // Idempotent — second destroy is silently a no-op.
        destroyConstraint(*backend, world, joint);
        backend->destroyBody(world, a);
        backend->destroyBody(world, b);
    }

    // Case 4: zero-id / invalid-world destroy is a no-op.
    destroyConstraint(*backend, world, JointId{});
    destroyConstraint(*backend, PhysicsWorldId{}, JointId{42});

    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
