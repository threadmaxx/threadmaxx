#include "Check.hpp"

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P2 — two bodies share one ShapeId. The shape stays alive as long as
// any body references it; destroying the last referent (with a prior
// `destroyShape` request) actually frees it. This is the deferred-
// destroy contract documented on `IPhysicsBackend::destroyShape`.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);
    CHECK(static_cast<bool>(world));

    ShapeDesc sd;
    sd.type = ShapeType::Box;
    sd.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(sd);
    CHECK(static_cast<bool>(shape));

    // Two bodies, both holding the same shape.
    BodyDesc bd1;
    bd1.position = Vec3{0.0f, 0.0f, 0.0f};
    ShapeId shapeArr[1] = {shape};
    BodyId body1 = backend->createBody(world, bd1,
                                       std::span<const ShapeId>(shapeArr, 1));
    CHECK(static_cast<bool>(body1));

    BodyDesc bd2;
    bd2.position = Vec3{1.0f, 0.0f, 0.0f};
    BodyId body2 = backend->createBody(world, bd2,
                                       std::span<const ShapeId>(shapeArr, 1));
    CHECK(static_cast<bool>(body2));

    // Caller asks for destroy while two bodies still reference it.
    // Shape must remain queryable.
    backend->destroyShape(shape);
    CHECK(backend->getShapeDesc(shape) != nullptr);

    // Drop one body — refcount goes to 1; shape still alive.
    backend->destroyBody(world, body1);
    CHECK(backend->getShapeDesc(shape) != nullptr);

    // Drop the second — refcount hits 0, pendingDestroy fires,
    // shape is actually freed.
    backend->destroyBody(world, body2);
    CHECK(backend->getShapeDesc(shape) == nullptr);
    CHECK(!backend->getShapeAabb(shape).has_value());

    // Sanity: a fresh shape after the cycle recycles the same slot.
    // The handle's generation must differ so the stale `shape` id
    // can't alias the new one.
    ShapeId fresh = backend->createShape(sd);
    CHECK(static_cast<bool>(fresh));
    CHECK(fresh.value != shape.value);
    CHECK(backend->getShapeDesc(shape) == nullptr);
    CHECK(backend->getShapeDesc(fresh) != nullptr);

    // Counter-case: destroyBody without a prior destroyShape leaves
    // the shape alive (refcount drops to 0 but pendingDestroy is false).
    ShapeId persisted = backend->createShape(sd);
    BodyId transient = backend->createBody(
        world, bd1, std::span<const ShapeId>(&persisted, 1));
    backend->destroyBody(world, transient);
    CHECK(backend->getShapeDesc(persisted) != nullptr);

    // Cleanup.
    backend->destroyShape(fresh);
    backend->destroyShape(persisted);
    backend->destroyWorld(world);

    EXIT_WITH_RESULT();
}
