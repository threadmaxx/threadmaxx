// §3.1 Serialization + World::snapshot. Capture a world's dense
// arrays, serialize to a stream, deserialize, and verify round-trip
// integrity for every field type.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstring>
#include <sstream>

namespace {

class SeedGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        // 3 entities with distinct values across every component slot.
        threadmaxx::Transform t0;
        t0.position = {1.0f, 2.0f, 3.0f};
        t0.orientation = {0.1f, 0.2f, 0.3f, 0.9f};
        t0.scale = {1.0f, 1.0f, 1.0f};
        cb.spawn(t0,
                 threadmaxx::Velocity{{0.5f, 0.0f, -0.5f}, {}},
                 threadmaxx::RenderTag{7, 11, 0xAABBu},
                 threadmaxx::UserData{0xDEADBEEFu});

        threadmaxx::Transform t1;
        t1.position = {-4.0f, -5.0f, -6.0f};
        cb.spawn(t1, {}, {}, threadmaxx::UserData{42u},
                 threadmaxx::Acceleration{{0.0f, 9.81f, 0.0f}, {}});

        // Third entity: no RenderTag (negative meshId).
        cb.spawn({});
    }
};

bool transformsEqual(const threadmaxx::Transform& a, const threadmaxx::Transform& b) {
    return a.position.x    == b.position.x    && a.position.y    == b.position.y    && a.position.z    == b.position.z
        && a.orientation.x == b.orientation.x && a.orientation.y == b.orientation.y && a.orientation.z == b.orientation.z && a.orientation.w == b.orientation.w
        && a.scale.x       == b.scale.x       && a.scale.y       == b.scale.y       && a.scale.z       == b.scale.z;
}

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);
    SeedGame g;
    CHECK(engine.initialize(g));

    // World::snapshot() should capture all 3 entities.
    const WorldSnapshot snap = engine.world().snapshot();
    CHECK_EQ(snap.size(), std::size_t{3});
    CHECK_EQ(snap.entities.size(),      std::size_t{3});
    CHECK_EQ(snap.transforms.size(),    std::size_t{3});
    CHECK_EQ(snap.velocities.size(),    std::size_t{3});
    CHECK_EQ(snap.renderTags.size(),    std::size_t{3});
    CHECK_EQ(snap.userData.size(),      std::size_t{3});
    CHECK_EQ(snap.accelerations.size(), std::size_t{3});
    CHECK_EQ(snap.parents.size(),       std::size_t{3});
    CHECK_EQ(snap.masks.size(),         std::size_t{3});

    // Field values from the first entity.
    CHECK_EQ(snap.transforms[0].position.x, 1.0f);
    CHECK_EQ(snap.transforms[0].position.y, 2.0f);
    CHECK_EQ(snap.transforms[0].position.z, 3.0f);
    CHECK_EQ(snap.velocities[0].linear.x,   0.5f);
    CHECK_EQ(snap.renderTags[0].meshId,     std::int32_t{7});
    CHECK_EQ(snap.renderTags[0].materialId, std::int32_t{11});
    CHECK_EQ(snap.userData[0].value,        std::uint64_t{0xDEADBEEFu});

    // Second entity carries Acceleration with non-default values.
    CHECK_EQ(snap.accelerations[1].linear.y, 9.81f);
    CHECK_EQ(snap.userData[1].value,         std::uint64_t{42u});

    // Third entity: RenderTag bit must NOT be set in the mask.
    CHECK(!snap.masks[2].has(Component::RenderTag));
    // First two are renderable.
    CHECK(snap.masks[0].has(Component::RenderTag));

    // Round-trip through binary stream.
    std::ostringstream out;
    serialize(out, snap);
    const std::string blob = out.str();
    CHECK(!blob.empty());

    WorldSnapshot decoded;
    std::istringstream in(blob);
    CHECK(deserialize(in, decoded));

    // Every array matches exactly.
    CHECK_EQ(decoded.size(), snap.size());
    for (std::size_t i = 0; i < snap.size(); ++i) {
        CHECK(decoded.entities[i] == snap.entities[i]);
        CHECK(transformsEqual(decoded.transforms[i], snap.transforms[i]));
        CHECK_EQ(decoded.velocities[i].linear.x,  snap.velocities[i].linear.x);
        CHECK_EQ(decoded.renderTags[i].meshId,    snap.renderTags[i].meshId);
        CHECK_EQ(decoded.renderTags[i].materialId,snap.renderTags[i].materialId);
        CHECK_EQ(decoded.userData[i].value,       snap.userData[i].value);
        CHECK_EQ(decoded.accelerations[i].linear.y,snap.accelerations[i].linear.y);
        CHECK_EQ(decoded.masks[i].bits(),         snap.masks[i].bits());
    }

    // Per-component serialization round-trip (one of each type).
    {
        std::ostringstream o;
        const Transform a{{1.5f, -2.5f, 0.25f}, {0, 0, 0, 1}, {2, 2, 2}};
        serialize(o, a);
        Transform b{};
        std::istringstream i(o.str());
        CHECK(deserialize(i, b));
        CHECK(transformsEqual(a, b));
    }
    {
        std::ostringstream o;
        ComponentSet a;
        a.add(Component::Transform).add(Component::Parent);
        serialize(o, a);
        ComponentSet b;
        std::istringstream i(o.str());
        CHECK(deserialize(i, b));
        CHECK_EQ(a.bits(), b.bits());
    }

    // Rejection: wrong magic header should fail.
    {
        std::string corrupt = blob;
        // Flip first byte; this scrambles the magic.
        corrupt[0] = static_cast<char>(corrupt[0] ^ 0xFF);
        std::istringstream i(corrupt);
        WorldSnapshot bad;
        CHECK(!deserialize(i, bad));
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
