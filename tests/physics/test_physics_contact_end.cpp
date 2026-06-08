#include "Check.hpp"

#include "threadmaxx_physics/contact.hpp"
#include "threadmaxx_physics/stub_backend.hpp"

#include <vector>

// P8 — `ContactPhase::End` fires exactly once when two overlapping
// bodies separate.
//
// Setup: two unit boxes initially at the same position (0,0,0). After
// the first stepWorld they're flagged as overlapping (Begin). Then we
// teleport body B far away via `setBodyTransform` and the next
// stepWorld observes the pair gone → End. The post-tear-down phase
// checks that End is fired exactly once.

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
    b.position = Vec3{0.0f, 0.0f, 0.0f};  // overlapping A from the start
    BodyId bodyB = backend->createBody(world, b,
                                       std::span<const ShapeId>(shapes, 1));

    std::vector<ContactEvent> events;
    setContactCallback(*backend, world,
                       [&events](const ContactEvent& ev) {
                           events.push_back(ev);
                       });

    // tick 1: Begin fires (initial overlap was disjoint before any
    // tick has run — `activeContacts` starts empty).
    backend->stepWorld(world, 0.1f);
    CHECK(events.size() == 1);
    CHECK(events[0].phase == ContactPhase::Begin);

    // Teleport B to (10, 0, 0) — well outside any AABB overlap with A.
    backend->setBodyTransform(world, bodyB,
                              Vec3{10.0f, 0.0f, 0.0f},
                              Quat{});

    // tick 2: pair has departed → End fires exactly once.
    backend->stepWorld(world, 0.1f);
    CHECK(events.size() == 2);
    CHECK(events[1].phase == ContactPhase::End);
    // End event carries the same canonical pair as the Begin event.
    CHECK(events[1].bodyA == events[0].bodyA);
    CHECK(events[1].bodyB == events[0].bodyB);

    // tick 3: still apart. No additional events.
    backend->stepWorld(world, 0.1f);
    CHECK(events.size() == 2);

    backend->destroyBody(world, bodyA);
    backend->destroyBody(world, bodyB);
    backend->destroyShape(shape);
    backend->destroyWorld(world);
    EXIT_WITH_RESULT();
}
