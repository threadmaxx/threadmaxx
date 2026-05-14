// §3.1 batch-6 prep: CommandBuffer::addComponent<T> / removeComponent<T>.
//
// Exercises the generic transition API across every built-in data
// component type. Verifies:
//   - addComponent attaches the presence bit unconditionally, even for
//     types whose value-specific setter would have cleared it
//     (RenderTag{-1}, Parent with invalid handle).
//   - addComponent writes the dense value (where applicable) so that the
//     subsequent World::get<T> returns it.
//   - removeComponent clears the presence bit.
//   - removeComponent is idempotent (removing twice is harmless).
//   - The transition composes with addTag / removeTag: independent bit
//     edits in the same tick all land.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

class SeedingGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        // One bare entity with the default-mask (Transform + Velocity +
        // UserData + Acceleration) — no RenderTag, no Parent, none of
        // the batch-5 slots.
        seed.spawn(threadmaxx::Transform{});
    }
};

// Single-shot recorder: runs once and asks gameplay code (via the
// supplied callable) what to put in the buffer.
template <typename Body>
class RecorderSystem : public threadmaxx::ISystem {
public:
    explicit RecorderSystem(Body b) : body_(std::move(b)) {}
    const char* name() const noexcept override { return "recorder"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done_) return;
        done_ = true;
        auto& b = body_;
        ctx.single([&b](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            b(r, cb);
        });
    }
private:
    Body body_;
    bool done_ = false;
};

template <typename Body>
auto makeRecorder(Body b) {
    return std::make_unique<RecorderSystem<Body>>(std::move(b));
}

class EmptyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    SeedingGame game;
    CHECK(engine.initialize(game));

    // Seed runs at initialize time → one entity exists already.
    const auto& w = engine.world();
    engine.step();   // commit the seed
    CHECK_EQ(w.entities().size(), std::size_t{1});
    const auto e = w.entities()[0];

    // Sanity: pre-flip state has no batch-5 slots.
    CHECK(!w.has<Health>(e));
    CHECK(!w.has<Faction>(e));
    CHECK(!w.has<BoundingVolume>(e));
    CHECK(!w.has<RenderTag>(e));
    CHECK(!w.has<Parent>(e));

    // 1) addComponent<T> attaches the bit and writes the value for every
    //    data component type, including ones whose setter would have
    //    conditioned the bit on the value (RenderTag{-1}, Parent{}).
    engine.registerSystem(makeRecorder(
        [e](Range, CommandBuffer& cb) {
            cb.addComponent<Health>(e, Health{42.0f, 100.0f});
            cb.addComponent<Faction>(e, Faction{7});
            cb.addComponent<BoundingVolume>(e, BoundingVolume{
                Vec3{-1, -1, -1}, Vec3{1, 1, 1}});
            cb.addComponent<AnimationStateRef>(e, AnimationStateRef{});
            cb.addComponent<PhysicsBodyRef>(e, PhysicsBodyRef{0xdead});
            cb.addComponent<NavAgentRef>(e, NavAgentRef{0xbeef});
            // RenderTag with sentinel meshId — addComponent must STILL
            // attach the bit (setRenderTag alone would clear it).
            cb.addComponent<RenderTag>(e, RenderTag{-1});
            // Parent with invalid handle — same story.
            cb.addComponent<Parent>(e, Parent{kInvalidEntity});
        }));
    engine.step();

    CHECK(w.has<Health>(e));
    CHECK_EQ(w.get<Health>(e).current, 42.0f);
    CHECK_EQ(w.get<Health>(e).max, 100.0f);
    CHECK(w.has<Faction>(e));
    CHECK_EQ(w.get<Faction>(e).id, std::uint32_t{7});
    CHECK(w.has<BoundingVolume>(e));
    CHECK(w.has<AnimationStateRef>(e));
    CHECK(w.has<PhysicsBodyRef>(e));
    CHECK_EQ(w.get<PhysicsBodyRef>(e).handle, std::uint64_t{0xdead});
    CHECK(w.has<NavAgentRef>(e));
    CHECK_EQ(w.get<NavAgentRef>(e).handle, std::uint64_t{0xbeef});
    CHECK(w.has<RenderTag>(e));
    CHECK_EQ(w.get<RenderTag>(e).meshId, std::int32_t{-1});
    CHECK(w.has<Parent>(e));

    // 2) removeComponent<T> clears the bit AND physically migrates the
    //    entity out of T's archetype chunk (§3.1 batch 6) — a
    //    subsequent `tryGetT(e)` returns nullptr.
    engine.registerSystem(makeRecorder(
        [e](Range, CommandBuffer& cb) {
            cb.removeComponent<Health>(e);
            cb.removeComponent<Faction>(e);
            cb.removeComponent<RenderTag>(e);
            cb.removeComponent<Parent>(e);
            // Idempotent: removing something that isn't there is a
            // no-op, not an error.
            cb.removeComponent<Faction>(e);
        }));
    engine.step();
    CHECK(!w.has<Health>(e));
    CHECK(!w.has<Faction>(e));
    CHECK(!w.has<RenderTag>(e));
    CHECK(!w.has<Parent>(e));
    // §3.1 batch 6: removeComponent physically migrates the entity out
    // of T's archetype, so `tryGetT` now returns nullptr after a
    // removal (the mask bit IS the source of truth, and the chunked
    // storage no longer keeps the stale value around).
    CHECK_EQ(w.tryGetHealth(e), static_cast<const Health*>(nullptr));
    CHECK_EQ(w.tryGetFaction(e), static_cast<const Faction*>(nullptr));
    CHECK_EQ(w.tryGetParent(e), static_cast<const Parent*>(nullptr));

    // 3) Round-trip: add then remove then add again — final state has
    //    the bit and the new value.
    engine.registerSystem(makeRecorder(
        [e](Range, CommandBuffer& cb) {
            cb.addComponent<Health>(e, Health{1.0f, 1.0f});
            cb.removeComponent<Health>(e);
            cb.addComponent<Health>(e, Health{99.0f, 200.0f});
        }));
    engine.step();
    CHECK(w.has<Health>(e));
    CHECK_EQ(w.get<Health>(e).current, 99.0f);
    CHECK_EQ(w.get<Health>(e).max, 200.0f);

    // 4) Compose with addTag: the bit-only StaticTag rides through
    //    independently of the data-component transitions.
    engine.registerSystem(makeRecorder(
        [e](Range, CommandBuffer& cb) {
            cb.addTag(e, Component::StaticTag);
            cb.removeComponent<Health>(e);
        }));
    engine.step();
    CHECK(w.hasTag(e, Component::StaticTag));
    CHECK(!w.has<Health>(e));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
