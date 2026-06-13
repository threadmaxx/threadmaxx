/// @file test_physics_diagnostics.cpp
/// @brief P10 — IPhysicsBackend::worldStats returns a per-world POD
/// containing body / constraint / active-contact counts. Implemented
/// by StubBackend; default impl returns zeros (verified via passing
/// an invalid world id).

#include "Check.hpp"

#include <threadmaxx_physics/body.hpp>
#include <threadmaxx_physics/config.hpp>
#include <threadmaxx_physics/diagnostics.hpp>
#include <threadmaxx_physics/shape.hpp>
#include <threadmaxx_physics/stub_backend.hpp>

int main() {
    using namespace threadmaxx::physics;

    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

    PhysicsConfig cfg{};
    auto world = backend->createWorld(cfg);
    CHECK(world.value != 0);

    // Empty baseline.
    auto s = sampleWorldStats(*backend, world);
    CHECK_EQ(s.bodyCount, 0u);
    CHECK_EQ(s.constraintCount, 0u);
    CHECK_EQ(s.activeContactCount, 0u);

    // Add a couple of bodies, each with a tiny sphere shape.
    ShapeDesc sd{};
    sd.type = ShapeType::Sphere;
    sd.radius = 0.5f;
    auto shape = backend->createShape(sd);
    CHECK(shape.value != 0);

    BodyDesc bd{};
    bd.position = {0.0f, 0.0f, 0.0f};
    auto b1 = backend->createBody(world, bd, std::span<const ShapeId>{&shape, 1});
    bd.position = {10.0f, 0.0f, 0.0f};
    auto b2 = backend->createBody(world, bd, std::span<const ShapeId>{&shape, 1});
    CHECK(b1.value != 0);
    CHECK(b2.value != 0);

    s = sampleWorldStats(*backend, world);
    CHECK_EQ(s.bodyCount, 2u);
    CHECK_EQ(s.constraintCount, 0u);

    // Destroy one body — count drops.
    backend->destroyBody(world, b1);
    s = sampleWorldStats(*backend, world);
    CHECK_EQ(s.bodyCount, 1u);

    // Invalid world id falls through to the default zero-everything
    // POD via the no-such-world early return inside StubBackend.
    PhysicsWorldId bogus{};
    auto z = sampleWorldStats(*backend, bogus);
    CHECK_EQ(z.bodyCount, 0u);
    CHECK_EQ(z.constraintCount, 0u);

    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
