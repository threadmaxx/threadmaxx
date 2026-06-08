#include "Check.hpp"

#include "threadmaxx_physics/contact.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <vector>

// P8 — bodies in continuous overlap don't re-fire `Begin` every tick.
// After the initial Begin, subsequent stepWorld calls observe the pair
// in `activeContacts` and skip the event emission entirely (no Persist
// phase exists).
//
// Setup: two static unit boxes positioned so their AABBs overlap
// (centers at (0,0,0) and (0.5,0,0); AABBs [-0.5, 0.5] and [0, 1]
// overlap on x∈[0, 0.5]). Step 10 times → exactly one Begin event.
// Then move one body away and step → one End event. Then step
// further → no more events.

using namespace threadmaxx::physics;

int main() {
    auto backend = makeStubBackend();
    PhysicsConfig cfg;
    PhysicsWorldId world = backend->createWorld(cfg);

    ShapeDesc box;
    box.type = ShapeType::Box;
    box.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    ShapeId shape = backend->createShape(box);
    ShapeId shapes[1] = {shape};

    BodyDesc a;
    a.type = BodyType::Static;
    a.position = Vec3{0.0f, 0.0f, 0.0f};
    BodyId bodyA = backend->createBody(world, a,
                                       std::span<const ShapeId>(shapes, 1));

    BodyDesc b;
    b.type = BodyType::Kinematic;
    b.position = Vec3{0.5f, 0.0f, 0.0f};  // overlaps A on x∈[0, 0.5]
    BodyId bodyB = backend->createBody(world, b,
                                       std::span<const ShapeId>(shapes, 1));

    std::vector<ContactEvent> events;
    setContactCallback(*backend, world,
                       [&events](const ContactEvent& ev) {
                           events.push_back(ev);
                       });

    // 10 ticks of continuous overlap — Begin must fire exactly once at
    // tick 1, then the same pair stays in `activeContacts` and the
    // diff skips it.
    for (int i = 0; i < 10; ++i) {
        backend->stepWorld(world, 0.1f);
    }
    CHECK(events.size() == 1);
    CHECK(events[0].phase == ContactPhase::Begin);

    // Move B away → next step fires exactly one End event.
    backend->setBodyTransform(world, bodyB,
                              Vec3{10.0f, 0.0f, 0.0f},
                              Quat{});
    backend->stepWorld(world, 0.1f);
    CHECK(events.size() == 2);
    CHECK(events[1].phase == ContactPhase::End);

    // 5 more idle ticks → no spurious events of any kind.
    for (int i = 0; i < 5; ++i) {
        backend->stepWorld(world, 0.1f);
    }
    CHECK(events.size() == 2);

    backend->destroyBody(world, bodyA);
    backend->destroyBody(world, bodyB);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
