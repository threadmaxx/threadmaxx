#include "Check.hpp"

#include "threadmaxx_physics/query.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <cmath>

// P5 — closest-hit raycast: ray origin (-5,0,0) heading +X must hit a
// unit box at origin on its -X face at x = -0.5. Distance = 4.5;
// position = (-0.5, 0, 0); normal points back toward the ray, i.e.
// (-1, 0, 0).

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

    BodyDesc bd;
    bd.type = BodyType::Static;
    bd.position = Vec3{0.0f, 0.0f, 0.0f};
    ShapeId shapes[1] = {shape};
    BodyId body = backend->createBody(world, bd,
                                      std::span<const ShapeId>(shapes, 1));

    RaycastRequest req;
    req.origin = Vec3{-5.0f, 0.0f, 0.0f};
    req.direction = Vec3{1.0f, 0.0f, 0.0f};

    auto hit = raycast(*backend, world, req);
    CHECK(hit.has_value());
    CHECK(hit->body == body);
    CHECK(nearly(hit->distance, 4.5f));
    CHECK(nearly(hit->position.x, -0.5f));
    CHECK(nearly(hit->position.y, 0.0f));
    CHECK(nearly(hit->position.z, 0.0f));
    CHECK(nearly(hit->normal.x, -1.0f));
    CHECK(nearly(hit->normal.y, 0.0f));
    CHECK(nearly(hit->normal.z, 0.0f));

    // Hit from +Y instead — ray (0, 5, 0) heading -Y lands on +Y face
    // at y = 0.5; normal should point back along +Y.
    RaycastRequest topDown;
    topDown.origin = Vec3{0.0f, 5.0f, 0.0f};
    topDown.direction = Vec3{0.0f, -1.0f, 0.0f};
    auto topHit = raycast(*backend, world, topDown);
    CHECK(topHit.has_value());
    CHECK(nearly(topHit->distance, 4.5f));
    CHECK(nearly(topHit->position.y, 0.5f));
    CHECK(nearly(topHit->normal.y, 1.0f));

    // maxDistance shorter than the actual hit distance → miss.
    RaycastRequest tooShort = req;
    tooShort.maxDistance = 3.0f;
    auto shortHit = raycast(*backend, world, tooShort);
    CHECK(!shortHit.has_value());

    backend->destroyBody(world, body);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
