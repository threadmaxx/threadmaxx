#include "Check.hpp"

#include "threadmaxx_physics/jolt_backend.hpp"
#include "threadmaxx_physics/threadmaxx_physics.hpp"

// P9 — Jolt backend smoke test.
//
// Drop a sphere from y=10 onto a static ground plane (modelled as a
// large thin box) under standard gravity. After 60 simulation ticks at
// dt=1/60s (≈ 1 second of falling) the sphere has fallen at least a
// meter from its starting height. We don't pin a closed-form expected
// position because Jolt's solver and our integration choices (substeps,
// damping defaults) are not bit-deterministic against the StubBackend —
// the gate is "did gravity work at all", which is what `smoke` covers.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeJoltBackend();
    CHECK(backend != nullptr);
    if (!backend) {
        // Defensive: gate-off path; the test file is only registered
        // with CTest when THREADMAXX_PHYSICS_HAS_JOLT is set, but make
        // failure mode obvious if somebody bypasses CMake.
        EXIT_WITH_RESULT();
    }

    PhysicsConfig cfg;
    cfg.fixedTimestep = 1.0f / 60.0f;
    cfg.maxSubSteps = 1;
    // Deterministic profile — see test_physics_jolt_conformance.
    cfg.allowSolverThreading = false;
    PhysicsWorldId world = backend->createWorld(cfg);
    CHECK(world.value != 0);

    // Static ground at y=0 — large flat box.
    ShapeDesc groundDesc;
    groundDesc.type = ShapeType::Box;
    groundDesc.halfExtents = Vec3{50.0f, 0.5f, 50.0f};
    ShapeId groundShape = backend->createShape(groundDesc);
    CHECK(groundShape.value != 0);

    BodyDesc groundBody;
    groundBody.type = BodyType::Static;
    groundBody.position = Vec3{0.0f, -0.5f, 0.0f};  // top surface at y=0
    ShapeId groundShapes[1] = {groundShape};
    BodyId ground = backend->createBody(world, groundBody,
                                        std::span<const ShapeId>(groundShapes, 1));
    CHECK(ground.value != 0);

    // Dynamic sphere of radius 0.5m starting at y=10.
    ShapeDesc sphereDesc;
    sphereDesc.type = ShapeType::Sphere;
    sphereDesc.radius = 0.5f;
    ShapeId sphereShape = backend->createShape(sphereDesc);
    CHECK(sphereShape.value != 0);

    BodyDesc sphereBody;
    sphereBody.type = BodyType::Dynamic;
    sphereBody.position = Vec3{0.0f, 10.0f, 0.0f};
    sphereBody.mass = 1.0f;
    ShapeId sphereShapes[1] = {sphereShape};
    BodyId sphere = backend->createBody(world, sphereBody,
                                        std::span<const ShapeId>(sphereShapes, 1));
    CHECK(sphere.value != 0);

    auto initialState = backend->getBodyState(world, sphere);
    CHECK(initialState.has_value());
    const float startY = initialState->position.y;

    // Run 60 ticks (~1 second of sim).
    for (int i = 0; i < 60; ++i) {
        backend->stepWorld(world, cfg.fixedTimestep);
    }

    auto finalState = backend->getBodyState(world, sphere);
    CHECK(finalState.has_value());

    // Gravity worked: the sphere fell from its starting height. With g
    // = -9.81 m/s² over 1 s of free fall it should have travelled ~4.9
    // m in the absence of ground; with the ground at y=0 it should be
    // resting (or near-resting) somewhere between y=0 (ground) and
    // y=10-1=9. We just check the loose "moved down by at least 1m"
    // gate so this isn't sensitive to substep choice / solver tuning.
    CHECK(finalState->position.y < startY - 1.0f);
    // And: didn't fall through the ground (sphere center should be
    // above its radius from the ground top).
    CHECK(finalState->position.y > -1.0f);

    backend->destroyBody(world, sphere);
    backend->destroyBody(world, ground);
    backend->destroyShape(sphereShape);
    backend->destroyShape(groundShape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
