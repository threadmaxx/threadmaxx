#include "Check.hpp"

#include "threadmaxx_physics/query.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <algorithm>

// P5 — overlap query: point / sphere center vs body AABBs. Both forms
// honor the layer filter and return every match (no closest-hit
// shortcut). Empty world / center in empty space returns an empty
// list; the buffer-reuse form clears the buffer before populating.

using namespace threadmaxx::physics;

namespace {
bool contains(const std::vector<BodyId>& v, BodyId b) {
    return std::find(v.begin(), v.end(), b) != v.end();
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

    // Two bodies at (0,0,0) and (5,0,0).
    BodyDesc a;
    a.type = BodyType::Static;
    a.position = Vec3{0.0f, 0.0f, 0.0f};
    ShapeId shapes[1] = {shape};
    BodyId bodyA = backend->createBody(world, a,
                                       std::span<const ShapeId>(shapes, 1));
    BodyDesc b;
    b.type = BodyType::Static;
    b.position = Vec3{5.0f, 0.0f, 0.0f};
    BodyId bodyB = backend->createBody(world, b,
                                       std::span<const ShapeId>(shapes, 1));

    // Point overlap at A's position → A only.
    OverlapRequest atA;
    atA.center = Vec3{0.0f, 0.0f, 0.0f};
    atA.radius = 0.0f;
    auto hits = overlapBodies(*backend, world, atA);
    CHECK(hits.size() == 1);
    CHECK(hits[0] == bodyA);

    // Point overlap in empty space → empty list.
    OverlapRequest empty;
    empty.center = Vec3{100.0f, 100.0f, 100.0f};
    empty.radius = 0.0f;
    auto noHits = overlapBodies(*backend, world, empty);
    CHECK(noHits.empty());

    // Sphere overlap large enough to span both bodies.
    OverlapRequest spanBoth;
    spanBoth.center = Vec3{2.5f, 0.0f, 0.0f};
    spanBoth.radius = 5.0f;
    auto bothHits = overlapBodies(*backend, world, spanBoth);
    CHECK(bothHits.size() == 2);
    CHECK(contains(bothHits, bodyA));
    CHECK(contains(bothHits, bodyB));

    // Buffer-reuse form clears the input before populating: pre-fill,
    // call with a query that hits nothing, expect the buffer empty.
    std::vector<BodyId> reuse;
    reuse.push_back(BodyId{12345}); // stale entry the caller forgot
    overlapBodies(*backend, world, empty, reuse);
    CHECK(reuse.empty());

    // Reuse again with a hit-yielding query.
    overlapBodies(*backend, world, atA, reuse);
    CHECK(reuse.size() == 1);
    CHECK(reuse[0] == bodyA);

    // Invalid world → empty (clears the buffer too).
    reuse.push_back(BodyId{99});
    overlapBodies(*backend, PhysicsWorldId{}, atA, reuse);
    CHECK(reuse.empty());

    backend->destroyBody(world, bodyA);
    backend->destroyBody(world, bodyB);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
