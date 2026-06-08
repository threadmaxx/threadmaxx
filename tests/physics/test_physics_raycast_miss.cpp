#include "Check.hpp"

#include "threadmaxx_physics/query.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P5 — raycast miss: rays that don't intersect any body's AABB, or
// that point the wrong way, return nullopt. Also covers the
// empty-world (no bodies) and stale-world-id edges.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc box;
    box.type = ShapeType::Box;
    box.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(box);

    BodyDesc bd;
    bd.type = BodyType::Static;
    bd.position = Vec3{0.0f, 0.0f, 0.0f};
    ShapeId shapes[1] = {shape};
    BodyId body = backend->createBody(world, bd,
                                      std::span<const ShapeId>(shapes, 1));

    // Ray pointing away from the only body (origin past the +X face,
    // direction further +X).
    RaycastRequest away;
    away.origin = Vec3{5.0f, 0.0f, 0.0f};
    away.direction = Vec3{1.0f, 0.0f, 0.0f};
    CHECK(!raycast(*backend, world, away).has_value());

    // Ray parallel to the box but offset along Y so it never enters
    // the Y slab.
    RaycastRequest miss;
    miss.origin = Vec3{-5.0f, 5.0f, 0.0f};
    miss.direction = Vec3{1.0f, 0.0f, 0.0f};
    CHECK(!raycast(*backend, world, miss).has_value());

    // Empty world (no bodies) — any ray misses.
    auto empty = makeStubBackend();
    PhysicsWorldId emptyWorld = empty->createWorld(cfg);
    RaycastRequest anyDir;
    anyDir.origin = Vec3{0.0f, 0.0f, 0.0f};
    anyDir.direction = Vec3{1.0f, 0.0f, 0.0f};
    CHECK(!raycast(*empty, emptyWorld, anyDir).has_value());

    // Invalid world handle — never crashes, returns nullopt.
    CHECK(!raycast(*backend, PhysicsWorldId{}, anyDir).has_value());

    empty->destroyWorld(emptyWorld);
    backend->destroyBody(world, body);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
