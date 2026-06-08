#include "Check.hpp"

#include "threadmaxx_physics/backend.hpp"
#include "threadmaxx_physics/stub_backend.hpp"
#include "threadmaxx_physics/sync.hpp"

#include <vector>

// P3 — batch body-state sync over 256 handles. Both the backend's
// direct `syncBodiesToGame` and the `sync.hpp` `syncBodyStates` wrapper
// must fill the output span in the same order as the input, with one
// `BodyState` per input handle.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    CHECK(backend != nullptr);

    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc sd;
    sd.type = ShapeType::Box;
    sd.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(sd);

    constexpr std::size_t kN = 256;
    std::vector<BodyId> bodies;
    bodies.reserve(kN);

    // Give each body a unique pose so we can verify alignment from
    // index to BodyState below.
    for (std::size_t i = 0; i < kN; ++i) {
        BodyDesc bd;
        bd.position = Vec3{static_cast<float>(i),
                           static_cast<float>(i) * 2.0f,
                           static_cast<float>(i) * 3.0f};
        bd.linearVelocity = Vec3{static_cast<float>(i) * 0.5f, 0.0f, 0.0f};
        bodies.push_back(backend->createBody(
            world, bd, std::span<const ShapeId>(&shape, 1)));
        CHECK(static_cast<bool>(bodies.back()));
    }

    // Direct backend path.
    std::vector<BodyState> direct(kN);
    backend->syncBodiesToGame(
        world,
        std::span<const BodyId>(bodies.data(), kN),
        std::span<BodyState>(direct.data(), kN));
    for (std::size_t i = 0; i < kN; ++i) {
        CHECK(direct[i].position.x == static_cast<float>(i));
        CHECK(direct[i].position.y == static_cast<float>(i) * 2.0f);
        CHECK(direct[i].position.z == static_cast<float>(i) * 3.0f);
        CHECK(direct[i].linearVelocity.x == static_cast<float>(i) * 0.5f);
    }

    // sync.hpp span wrapper — same result, ergonomic free function.
    std::vector<BodyState> viaWrapper(kN);
    syncBodyStates(*backend, world,
                   std::span<const BodyId>(bodies.data(), kN),
                   std::span<BodyState>(viaWrapper.data(), kN));
    for (std::size_t i = 0; i < kN; ++i) {
        CHECK(viaWrapper[i].position.x == direct[i].position.x);
        CHECK(viaWrapper[i].position.y == direct[i].position.y);
        CHECK(viaWrapper[i].position.z == direct[i].position.z);
        CHECK(viaWrapper[i].linearVelocity.x == direct[i].linearVelocity.x);
    }

    // sync.hpp vector-returning overload.
    auto owned = syncBodyStates(*backend, world,
                                std::span<const BodyId>(bodies.data(), kN));
    CHECK(owned.size() == kN);
    for (std::size_t i = 0; i < kN; ++i) {
        CHECK(owned[i].position.x == static_cast<float>(i));
        CHECK(owned[i].position.y == static_cast<float>(i) * 2.0f);
        CHECK(owned[i].position.z == static_cast<float>(i) * 3.0f);
    }

    // Span-size mismatch is a documented no-op — the output buffer
    // must be left untouched.
    std::vector<BodyState> tooSmall(kN - 1);
    for (auto& s : tooSmall) {
        s.position = Vec3{42.0f, 42.0f, 42.0f};
    }
    backend->syncBodiesToGame(
        world,
        std::span<const BodyId>(bodies.data(), kN),
        std::span<BodyState>(tooSmall.data(), tooSmall.size()));
    for (const auto& s : tooSmall) {
        CHECK(s.position.x == 42.0f);
    }

    // Stale handles inside a batch produce default `BodyState{}` entries
    // (per the IPhysicsBackend::syncBodiesToGame contract).
    BodyId stale = bodies[10];
    backend->destroyBody(world, stale);
    std::vector<BodyState> mixed(kN);
    backend->syncBodiesToGame(
        world,
        std::span<const BodyId>(bodies.data(), kN),
        std::span<BodyState>(mixed.data(), kN));
    CHECK(mixed[10].position.x == 0.0f);
    CHECK(mixed[10].position.y == 0.0f);
    CHECK(mixed[10].position.z == 0.0f);
    // Neighbors are unaffected.
    CHECK(mixed[9].position.x == 9.0f);
    CHECK(mixed[11].position.x == 11.0f);

    backend->destroyWorld(world);
    backend->destroyShape(shape);

    EXIT_WITH_RESULT();
}
