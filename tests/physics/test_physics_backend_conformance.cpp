#include "Check.hpp"

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

// Drives the StubBackend through the abstract `IPhysicsBackend*`
// surface only — never names the concrete type. This is the test that
// future backend adapters (JoltBackend, Bullet, PhysX) MUST also pass.
// Each adapter that lands gets a sibling that constructs through its
// own factory and reruns the same body of assertions; the shared body
// can be lifted out if/when there are 3+ backends.

using namespace threadmaxx::physics;

namespace {

// Drives the supplied backend through the conformance scenario. Any
// `IPhysicsBackend*` should pass — Stub today, Jolt in P9, etc.
void exerciseBackend(IPhysicsBackend& backend) {
    PhysicsConfig cfg;
    cfg.fixedTimestep = 1.0f / 60.0f;
    cfg.maxSubSteps = 4;

    PhysicsWorldId world = backend.createWorld(cfg);
    CHECK(static_cast<bool>(world));

    ShapeDesc sphereDesc;
    sphereDesc.type = ShapeType::Sphere;
    sphereDesc.radius = 0.5f;
    ShapeId sphere = backend.createShape(sphereDesc);
    CHECK(static_cast<bool>(sphere));

    constexpr int kBodyCount = 8;
    BodyId bodies[kBodyCount] = {};
    for (int i = 0; i < kBodyCount; ++i) {
        BodyDesc bd;
        bd.type = (i % 2 == 0) ? BodyType::Dynamic : BodyType::Kinematic;
        bd.position = Vec3{static_cast<float>(i), 0.0f, 0.0f};
        bd.mass = 1.0f;
        ShapeId shapeArr[1] = {sphere};
        bodies[i] = backend.createBody(world, bd,
                                       std::span<const ShapeId>(shapeArr, 1));
        CHECK(static_cast<bool>(bodies[i]));
    }

    // Step once. The conformance contract is: stepWorld accepts a
    // valid world + dt, doesn't crash, and any subsequent
    // syncBodiesToGame call returns at least dt-coherent state. (P1
    // Stub doesn't integrate, P4 Stub will, real backends always do —
    // this test asserts the surface, not the integration semantics.)
    backend.stepWorld(world, 1.0f / 60.0f);

    // Batch sync: write all 8 bodies' states in one call.
    BodyState states[kBodyCount] = {};
    backend.syncBodiesToGame(
        world,
        std::span<const BodyId>(bodies, kBodyCount),
        std::span<BodyState>(states, kBodyCount));

    // Initial positions seeded `bd.position = Vec3{i, 0, 0}`. With
    // Stub the positions never move; for any backend, the original
    // x-coordinate must still be readable for static + kinematic
    // bodies (kinematics with zero velocity hold their position).
    for (int i = 0; i < kBodyCount; ++i) {
        if (i % 2 == 1) {
            CHECK(states[i].position.x == static_cast<float>(i));
        }
    }

    // Mismatched span sizes are a defensive no-op (release behavior).
    BodyState tooSmall[2] = {};
    backend.syncBodiesToGame(world,
                             std::span<const BodyId>(bodies, kBodyCount),
                             std::span<BodyState>(tooSmall, 2));
    CHECK(tooSmall[0].position.x == 0.0f);

    // Tear down in reverse order.
    for (int i = kBodyCount - 1; i >= 0; --i) {
        backend.destroyBody(world, bodies[i]);
    }
    backend.destroyShape(sphere);
    backend.destroyWorld(world);
}

} // namespace

int main() {
    auto stub = makeStubBackend();
    CHECK(stub != nullptr);

    // Drive through the abstract base — the concrete type name never
    // appears below this line.
    IPhysicsBackend& backend = *stub;
    exerciseBackend(backend);

    // After teardown the backend is still usable: a second world cycle
    // proves the internal tables recycle slots cleanly.
    exerciseBackend(backend);

    EXIT_WITH_RESULT();
}
