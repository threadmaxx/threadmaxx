#include "Check.hpp"

#include "threadmaxx_physics/stub_backend.hpp"

#include <cstring>

// P4 — determinism: two independent backends, identical setup, run the
// same step sequence side-by-side. Final `BodyState` bytes must match
// exactly. This pins the floor-line every higher backend (notably
// JoltBackend in P9 under its determinism profile) has to meet for
// lockstep / replay parity.

using namespace threadmaxx::physics;

namespace {

struct Setup {
    BodyDesc desc;
    int ticks;
    float dt;
};

BodyState runScene(IPhysicsBackend& backend, const Setup& s) {
    PhysicsConfig cfg;
    PhysicsWorldId world = backend.createWorld(cfg);
    BodyId body = backend.createBody(world, s.desc,
                                     std::span<const ShapeId>{});
    for (int i = 0; i < s.ticks; ++i) {
        backend.stepWorld(world, s.dt);
    }
    BodyState out{};
    BodyId one[1] = {body};
    BodyState slot[1] = {};
    backend.syncBodiesToGame(world, std::span<const BodyId>(one, 1),
                             std::span<BodyState>(slot, 1));
    out = slot[0];
    backend.destroyBody(world, body);
    backend.destroyWorld(world);
    return out;
}

bool bytesEqual(const BodyState& a, const BodyState& b) {
    return std::memcmp(&a, &b, sizeof(BodyState)) == 0;
}

} // namespace

int main() {
    Setup setup{};
    setup.desc.type = BodyType::Dynamic;
    setup.desc.position = Vec3{1.0f, -2.0f, 3.0f};
    setup.desc.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    setup.desc.linearVelocity = Vec3{0.5f, 0.25f, -0.125f};
    setup.desc.angularVelocity = Vec3{0.1f, 0.2f, 0.3f};
    setup.ticks = 60;
    setup.dt = 1.0f / 60.0f;

    auto a = makeStubBackend();
    auto b = makeStubBackend();

    BodyState sa = runScene(*a, setup);
    BodyState sb = runScene(*b, setup);

    // Byte-exact match across independent backend instances. The Stub
    // is single-threaded, deterministic-by-construction; this is the
    // baseline the conformance gate for real backends measures against.
    CHECK(bytesEqual(sa, sb));

    // Same backend, second world, same setup — must also match.
    auto c = makeStubBackend();
    BodyState sc1 = runScene(*c, setup);
    BodyState sc2 = runScene(*c, setup);
    CHECK(bytesEqual(sc1, sc2));

    // Cross-backend AND cross-world parity in the same comparison.
    CHECK(bytesEqual(sa, sc1));

    // Sanity: at least one of the integrated fields actually moved off
    // the create-time pose — if both were zero the test above would
    // pass trivially.
    CHECK(sa.position.x != setup.desc.position.x ||
          sa.position.y != setup.desc.position.y ||
          sa.position.z != setup.desc.position.z);

    EXIT_WITH_RESULT();
}
