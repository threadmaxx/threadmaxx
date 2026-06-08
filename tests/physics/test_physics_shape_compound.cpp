#include "Check.hpp"

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P2 — a compound shape is composed of multiple primitive children.
// Bounds union over the children, and the parent holds a refcount on
// each child so the children survive until the parent is freed.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

    // Two box primitives with different extents.
    //   c1: half-extents (1,1,1)     → AABB [-1,-1,-1]..[+1,+1,+1]
    //   c2: half-extents (2,0.5,0.5) → AABB [-2,-.5,-.5]..[+2,+.5,+.5]
    ShapeDesc cd1;
    cd1.type = ShapeType::Box;
    cd1.halfExtents = Vec3{1.0f, 1.0f, 1.0f};
    ShapeId c1 = backend->createShape(cd1);

    ShapeDesc cd2;
    cd2.type = ShapeType::Box;
    cd2.halfExtents = Vec3{2.0f, 0.5f, 0.5f};
    ShapeId c2 = backend->createShape(cd2);

    // Compound parent referencing both children.
    ShapeDesc compoundDesc;
    compoundDesc.type = ShapeType::Compound;
    compoundDesc.children = {c1, c2};
    ShapeId compound = backend->createShape(compoundDesc);
    CHECK(static_cast<bool>(compound));

    // Bounds union: x from c2, y/z from c1.
    auto aabb = backend->getShapeAabb(compound);
    CHECK(aabb.has_value());
    CHECK(aabb->min.x == -2.0f);
    CHECK(aabb->min.y == -1.0f);
    CHECK(aabb->min.z == -1.0f);
    CHECK(aabb->max.x == 2.0f);
    CHECK(aabb->max.y == 1.0f);
    CHECK(aabb->max.z == 1.0f);

    // Asking to destroy c1 while the compound holds a ref must defer.
    backend->destroyShape(c1);
    CHECK(backend->getShapeDesc(c1) != nullptr);

    // Compound itself has refcount 0 (no body holds it) so destroying
    // it frees immediately AND cascades: c1 (pendingDestroy + refcount
    // → 0) is freed; c2 (no pendingDestroy) stays alive.
    backend->destroyShape(compound);
    CHECK(backend->getShapeDesc(compound) == nullptr);
    CHECK(backend->getShapeDesc(c1) == nullptr);
    CHECK(backend->getShapeDesc(c2) != nullptr);

    // The compound's AABB must be reported as empty once it's freed.
    CHECK(!backend->getShapeAabb(compound).has_value());

    // Cleanup the orphaned c2.
    backend->destroyShape(c2);
    CHECK(backend->getShapeDesc(c2) == nullptr);

    // Cascading scenario: build a compound, attach it to a body, then
    // destroyShape the compound. The body keeps the parent (and
    // therefore its children) alive until the body itself dies.
    ShapeDesc smallBox;
    smallBox.type = ShapeType::Box;
    smallBox.halfExtents = Vec3{0.25f, 0.25f, 0.25f};
    ShapeId leafA = backend->createShape(smallBox);
    ShapeId leafB = backend->createShape(smallBox);

    ShapeDesc compositeDesc;
    compositeDesc.type = ShapeType::Compound;
    compositeDesc.children = {leafA, leafB};
    ShapeId composite = backend->createShape(compositeDesc);

    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);
    BodyDesc bd;
    bd.position = Vec3{0.0f, 5.0f, 0.0f};
    BodyId body = backend->createBody(
        world, bd, std::span<const ShapeId>(&composite, 1));
    CHECK(static_cast<bool>(body));

    backend->destroyShape(composite); // pending while body holds ref
    backend->destroyShape(leafA);     // pending while composite holds ref
    backend->destroyShape(leafB);     // pending while composite holds ref
    CHECK(backend->getShapeDesc(composite) != nullptr);
    CHECK(backend->getShapeDesc(leafA) != nullptr);
    CHECK(backend->getShapeDesc(leafB) != nullptr);

    // Killing the body unwinds the whole tree.
    backend->destroyBody(world, body);
    CHECK(backend->getShapeDesc(composite) == nullptr);
    CHECK(backend->getShapeDesc(leafA) == nullptr);
    CHECK(backend->getShapeDesc(leafB) == nullptr);

    backend->destroyWorld(world);

    EXIT_WITH_RESULT();
}
