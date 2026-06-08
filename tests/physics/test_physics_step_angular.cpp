#include "Check.hpp"

#include "threadmaxx_physics/stub_backend.hpp"

#include <cmath>

// P4 — angular kinematic integration: orientation composes through
// axis-angle increments built from `angularVelocity * dt`. A body with
// omega = (0, π, 0) rad/s stepped over 1 second must rotate to 180°
// around Y, i.e. quat (0, 1, 0, 0) starting from identity.
//
// Tolerance: 60-substep integration multiplies 60 unit quaternions, each
// with a sin/cos rounding error around 1 ulp. 1e-5 absolute is the
// post-multiply budget on a stock x86-64 libm.

using namespace threadmaxx::physics;

namespace {

constexpr float kPi = 3.14159265358979323846f;

bool nearly(float a, float b, float tol = 1e-5f) {
    return std::fabs(a - b) < tol;
}

} // namespace

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    BodyDesc bd;
    bd.type = BodyType::Dynamic;
    bd.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    bd.angularVelocity = Vec3{0.0f, kPi, 0.0f};
    BodyId body = backend->createBody(world, bd, std::span<const ShapeId>{});

    // 60 ticks of 1/60s → 1 second of simulated time → π rad around Y.
    for (int i = 0; i < 60; ++i) {
        backend->stepWorld(world, 1.0f / 60.0f);
    }

    auto state = backend->getBodyState(world, body);
    CHECK(state.has_value());
    // 180° around Y as a unit quat is (0, 1, 0, 0).
    CHECK(nearly(state->rotation.x, 0.0f));
    CHECK(nearly(state->rotation.y, 1.0f));
    CHECK(nearly(state->rotation.z, 0.0f));
    CHECK(nearly(state->rotation.w, 0.0f));
    // Angular velocity preserved (no damping in the kinematic
    // integrator).
    CHECK(nearly(state->angularVelocity.y, kPi));

    // Single-shot dt = 1.0 also produces 180° around Y. (The angle is
    // exactly π, so sin(π/2)=1 and cos(π/2) is the libm result —
    // numerically not zero but within 1 ulp.)
    auto backend2 = makeStubBackend();
    PhysicsWorldId w2 = backend2->createWorld(cfg);
    BodyId b2 = backend2->createBody(w2, bd, std::span<const ShapeId>{});
    backend2->stepWorld(w2, 1.0f);
    auto s2 = backend2->getBodyState(w2, b2);
    CHECK(s2.has_value());
    CHECK(nearly(s2->rotation.x, 0.0f));
    CHECK(nearly(s2->rotation.y, 1.0f));
    CHECK(nearly(s2->rotation.z, 0.0f));
    CHECK(nearly(s2->rotation.w, 0.0f));

    // Zero angular velocity → orientation unchanged at full precision.
    BodyDesc still;
    still.type = BodyType::Dynamic;
    still.rotation = Quat{0.0f, 0.7071068f, 0.0f, 0.7071068f};
    still.angularVelocity = Vec3{0.0f, 0.0f, 0.0f};
    BodyId stillBody = backend->createBody(world, still,
                                           std::span<const ShapeId>{});
    backend->stepWorld(world, 1.0f);
    auto stillState = backend->getBodyState(world, stillBody);
    CHECK(stillState.has_value());
    CHECK(stillState->rotation.x == still.rotation.x);
    CHECK(stillState->rotation.y == still.rotation.y);
    CHECK(stillState->rotation.z == still.rotation.z);
    CHECK(stillState->rotation.w == still.rotation.w);

    backend->destroyBody(world, body);
    backend->destroyBody(world, stillBody);
    backend->destroyWorld(world);
    backend2->destroyBody(w2, b2);
    backend2->destroyWorld(w2);

    EXIT_WITH_RESULT();
}
