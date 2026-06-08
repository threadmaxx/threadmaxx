#include "Check.hpp"

#include "threadmaxx_physics/query.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <cmath>

// P5 — sphere sweep: clear corridor → no hit; sweep into a body
// returns the entry point. The stub inflates each body's world-space
// AABB by the sphere radius (Minkowski sum approximation), then
// performs the same slab raycast as the raycast path.

using namespace threadmaxx::physics;

namespace {
bool nearly(float a, float b, float tol = 1e-5f) {
    return std::fabs(a - b) < tol;
}
} // namespace

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc box;
    box.type = ShapeType::Box;
    box.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(box);

    // Body off to the side at (10, 0, 0); sweep at y = 5 along +X
    // passes far above the box → clear path.
    BodyDesc bd;
    bd.type = BodyType::Static;
    bd.position = Vec3{10.0f, 0.0f, 0.0f};
    ShapeId shapes[1] = {shape};
    BodyId body = backend->createBody(world, bd,
                                      std::span<const ShapeId>(shapes, 1));

    SweepRequest clear;
    clear.radius = 0.25f;
    clear.start = Vec3{-5.0f, 5.0f, 0.0f};
    clear.direction = Vec3{1.0f, 0.0f, 0.0f};
    clear.maxDistance = 100.0f;
    CHECK(!sweep(*backend, world, clear).has_value());

    // Sweep directly toward the body — sphere of radius 0.25 cast from
    // (-5, 0, 0) along +X. Box at (10,0,0) ± 0.5; inflated to (10,0,0)
    // ± 0.75. Entry at x = 9.25 → distance = 14.25.
    SweepRequest into;
    into.radius = 0.25f;
    into.start = Vec3{-5.0f, 0.0f, 0.0f};
    into.direction = Vec3{1.0f, 0.0f, 0.0f};
    into.maxDistance = 100.0f;
    auto hit = sweep(*backend, world, into);
    CHECK(hit.has_value());
    CHECK(hit->body == body);
    CHECK(nearly(hit->distance, 14.25f));
    // Position is the swept sphere's center at first contact.
    CHECK(nearly(hit->position.x, 9.25f));
    CHECK(nearly(hit->normal.x, -1.0f));

    // Sweep with radius 0 should match the raycast distance (10 - 0.5 - (-5) = 14.5).
    SweepRequest zeroR = into;
    zeroR.radius = 0.0f;
    auto zhit = sweep(*backend, world, zeroR);
    CHECK(zhit.has_value());
    CHECK(nearly(zhit->distance, 14.5f));

    // Sweep that just barely doesn't reach the inflated AABB.
    SweepRequest tooShort = into;
    tooShort.maxDistance = 14.0f;
    CHECK(!sweep(*backend, world, tooShort).has_value());

    backend->destroyBody(world, body);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
