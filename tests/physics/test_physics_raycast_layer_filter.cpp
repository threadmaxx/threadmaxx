#include "Check.hpp"

#include "threadmaxx_physics/query.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// P5 — layer mask filter: a body sits in `BodyDesc::layer` (0..31); the
// query passes `layerMask`. A body is considered iff
// `(1u << body.layer) & layerMask != 0`. Default-mask (all-ones) hits
// every body; targeted masks skip the layers they don't include.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc box;
    box.type = ShapeType::Box;
    box.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(box);

    // Body in layer 3.
    BodyDesc bd;
    bd.type = BodyType::Static;
    bd.position = Vec3{0.0f, 0.0f, 0.0f};
    bd.layer = 3u;
    ShapeId shapes[1] = {shape};
    BodyId body = backend->createBody(world, bd,
                                      std::span<const ShapeId>(shapes, 1));

    RaycastRequest req;
    req.origin = Vec3{-5.0f, 0.0f, 0.0f};
    req.direction = Vec3{1.0f, 0.0f, 0.0f};

    // Default all-ones mask — body is included.
    CHECK(raycast(*backend, world, req).has_value());

    // Mask that includes only layer 3 (bit 3 set) — hit.
    req.layerMask = (1u << 3u);
    auto hit = raycast(*backend, world, req);
    CHECK(hit.has_value());
    CHECK(hit->body == body);

    // Mask that excludes layer 3 (every bit except bit 3) — miss.
    req.layerMask = ~(1u << 3u);
    CHECK(!raycast(*backend, world, req).has_value());

    // Mask that includes only layer 0 — body sits in 3, miss.
    req.layerMask = (1u << 0u);
    CHECK(!raycast(*backend, world, req).has_value());

    // Mask zero — universal miss.
    req.layerMask = 0u;
    CHECK(!raycast(*backend, world, req).has_value());

    // Sweep and overlap honor the same filter.
    SweepRequest sweepReq;
    sweepReq.start = Vec3{-5.0f, 0.0f, 0.0f};
    sweepReq.direction = Vec3{1.0f, 0.0f, 0.0f};
    sweepReq.radius = 0.25f;
    sweepReq.layerMask = ~(1u << 3u);
    CHECK(!sweep(*backend, world, sweepReq).has_value());
    sweepReq.layerMask = (1u << 3u);
    CHECK(sweep(*backend, world, sweepReq).has_value());

    OverlapRequest overlapReq;
    overlapReq.center = Vec3{0.0f, 0.0f, 0.0f};
    overlapReq.radius = 0.0f;
    overlapReq.layerMask = ~(1u << 3u);
    std::vector<BodyId> hits;
    overlapBodies(*backend, world, overlapReq, hits);
    CHECK(hits.empty());
    overlapReq.layerMask = (1u << 3u);
    overlapBodies(*backend, world, overlapReq, hits);
    CHECK(hits.size() == 1);
    CHECK(hits[0] == body);

    backend->destroyBody(world, body);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
