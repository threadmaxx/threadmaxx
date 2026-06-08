#include "Check.hpp"

#include "threadmaxx_physics/stub_backend.hpp"
#include "threadmaxx_physics/step.hpp"

#include <cmath>

// P4 — linear kinematic integration: position advances by
// `linearVelocity * dt` every step. After 60 ticks of 1/60s with a
// constant velocity of (1, 0, 0) the body must land at (1, 0, 0) ± 1e-6.
// Also verifies the `step.hpp` `stepScene` wrapper produces the same
// result as the raw backend virtual.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    BodyDesc bd;
    bd.type = BodyType::Dynamic;
    bd.position = Vec3{0.0f, 0.0f, 0.0f};
    bd.linearVelocity = Vec3{1.0f, 0.0f, 0.0f};
    BodyId body = backend->createBody(world, bd, std::span<const ShapeId>{});

    // 60 × (1/60) = 1 second of simulated time.
    constexpr int kTicks = 60;
    constexpr float kDt = 1.0f / 60.0f;
    for (int i = 0; i < kTicks; ++i) {
        stepScene(*backend, world, kDt);
    }

    auto state = backend->getBodyState(world, body);
    CHECK(state.has_value());
    CHECK(std::fabs(state->position.x - 1.0f) < 1e-6f);
    CHECK(std::fabs(state->position.y) < 1e-6f);
    CHECK(std::fabs(state->position.z) < 1e-6f);
    // Velocity is preserved across integration.
    CHECK(state->linearVelocity.x == 1.0f);

    // Kinematic body, non-axis-aligned velocity — verifies vector add.
    BodyDesc kd;
    kd.type = BodyType::Kinematic;
    kd.position = Vec3{0.0f, 0.0f, 0.0f};
    kd.linearVelocity = Vec3{2.0f, -3.0f, 1.5f};
    BodyId kbody = backend->createBody(world, kd, std::span<const ShapeId>{});
    for (int i = 0; i < kTicks; ++i) {
        stepScene(*backend, world, kDt);
    }
    auto kstate = backend->getBodyState(world, kbody);
    CHECK(kstate.has_value());
    CHECK(std::fabs(kstate->position.x - 2.0f) < 1e-5f);
    CHECK(std::fabs(kstate->position.y - (-3.0f)) < 1e-5f);
    CHECK(std::fabs(kstate->position.z - 1.5f) < 1e-5f);

    // Single-shot dt = 1.0 gives the same result as 60×(1/60) for
    // linear motion (no rotation involved, so no float ordering
    // difference can leak in).
    auto backend2 = makeStubBackend();
    PhysicsWorldId w2 = backend2->createWorld(cfg);
    BodyId b2 = backend2->createBody(w2, bd, std::span<const ShapeId>{});
    backend2->stepWorld(w2, 1.0f);
    auto s2 = backend2->getBodyState(w2, b2);
    CHECK(s2.has_value());
    CHECK(std::fabs(s2->position.x - 1.0f) < 1e-6f);

    backend->destroyBody(world, body);
    backend->destroyBody(world, kbody);
    backend->destroyWorld(world);
    backend2->destroyBody(w2, b2);
    backend2->destroyWorld(w2);

    EXIT_WITH_RESULT();
}
