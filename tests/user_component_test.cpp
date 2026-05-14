// §3.1 batch 6b: UserComponent<T> extension hook.
//
// Game code registers POD types at runtime, then uses the same
// "addComponent / removeComponent" semantic as built-ins via the typed
// free functions in `<threadmaxx/UserComponent.hpp>`. This test pins:
//   - registerUserComponent returns a valid token (bit ≥ 16) and is
//     idempotent for the same typeid.
//   - addUserComponent attaches the bit and writes the value; user::has
//     reflects the bit, user::tryGet returns the stored value.
//   - addUserComponent triggers a physical archetype migration: the
//     entity moves into a chunk whose mask carries the user bit, and
//     user::chunkSpan reports a non-empty span there.
//   - removeUserComponent clears the bit; user::tryGet returns nullptr;
//     repeated remove is a no-op.
//   - Re-registering the same typeid yields the same id; two different
//     types get distinct bits.
//   - Round-trip after multiple migrations (add A, add B, remove A,
//     re-add A) preserves both values.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>

namespace {

struct AIBlackboard {
    std::int32_t  stateId   = 0;
    float         cooldown  = 0.0f;
    std::uint32_t target    = 0;
};
static_assert(sizeof(AIBlackboard) == 12, "");

struct LootRoll {
    std::uint64_t seed = 0;
};

class SeedGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        // Bare entity: default mask only.
        seed.spawn(threadmaxx::Transform{});
    }
};

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

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);

    // Register BEFORE initialize so the engine's registry is populated
    // before any spawn allocates chunks.
    const auto bbId   = engine.registerUserComponent<AIBlackboard>();
    const auto lootId = engine.registerUserComponent<LootRoll>();

    CHECK(bbId.valid());
    CHECK(lootId.valid());
    CHECK_EQ(bbId.stride,   std::uint32_t{sizeof(AIBlackboard)});
    CHECK_EQ(lootId.stride, std::uint32_t{sizeof(LootRoll)});
    // Bits ≥ 16 (built-ins occupy 0..15) and distinct.
    CHECK(bbId.bit   >= 16u);
    CHECK(lootId.bit >= 16u);
    CHECK(bbId.bit != lootId.bit);

    // Idempotent registration: same typeid → same token.
    const auto bbAgain = engine.registerUserComponent<AIBlackboard>();
    CHECK_EQ(bbAgain.bit,    bbId.bit);
    CHECK_EQ(bbAgain.stride, bbId.stride);

    SeedGame game;
    CHECK(engine.initialize(game));
    engine.step();   // commit seed
    const auto& w = engine.world();
    CHECK_EQ(w.entities().size(), std::size_t{1});
    const auto e = w.entities()[0];

    // Pre-state: neither user bit attached.
    CHECK(!user::has(w, bbId, e));
    CHECK(!user::has(w, lootId, e));
    CHECK_EQ(user::tryGet<AIBlackboard>(w, bbId, e),
             static_cast<const AIBlackboard*>(nullptr));

    // Attach AIBlackboard.
    engine.registerSystem(makeRecorder(
        [e, bbId](Range, CommandBuffer& cb) {
            addUserComponent<AIBlackboard>(cb, bbId, e,
                AIBlackboard{42, 1.5f, 7u});
        }));
    engine.step();

    CHECK(user::has(w, bbId, e));
    {
        const auto* bb = user::tryGet<AIBlackboard>(w, bbId, e);
        CHECK(bb != nullptr);
        CHECK_EQ(bb->stateId,  std::int32_t{42});
        CHECK_EQ(bb->cooldown, 1.5f);
        CHECK_EQ(bb->target,   std::uint32_t{7});
    }

    // chunkSpan: locate the entity's chunk and verify the span carries
    // the right typed value.
    {
        const auto loc = w.locate(e);
        CHECK(loc.valid());
        const auto& chunk = w.archetypeChunk(loc.archetype);
        const auto span = user::chunkSpan<AIBlackboard>(chunk, bbId);
        CHECK(!span.empty());
        CHECK_EQ(span.size(), chunk.entities.size());
        CHECK_EQ(span[loc.row].stateId, std::int32_t{42});
    }

    // Attach LootRoll on top of AIBlackboard — migrates into a new
    // archetype carrying both user bits. Both values must survive.
    engine.registerSystem(makeRecorder(
        [e, lootId](Range, CommandBuffer& cb) {
            addUserComponent<LootRoll>(cb, lootId, e, LootRoll{0xdeadbeefull});
        }));
    engine.step();

    CHECK(user::has(w, bbId, e));
    CHECK(user::has(w, lootId, e));
    {
        const auto* bb   = user::tryGet<AIBlackboard>(w, bbId, e);
        const auto* loot = user::tryGet<LootRoll>(w, lootId, e);
        CHECK(bb != nullptr);
        CHECK(loot != nullptr);
        CHECK_EQ(bb->stateId,   std::int32_t{42});
        CHECK_EQ(bb->cooldown,  1.5f);
        CHECK_EQ(loot->seed,    std::uint64_t{0xdeadbeefull});
    }

    // Remove AIBlackboard — migrates out of bb's archetype; LootRoll
    // value must persist across that migration.
    engine.registerSystem(makeRecorder(
        [e, bbId](Range, CommandBuffer& cb) {
            removeUserComponent(cb, bbId, e);
        }));
    engine.step();

    CHECK(!user::has(w, bbId, e));
    CHECK(user::has(w, lootId, e));
    CHECK_EQ(user::tryGet<AIBlackboard>(w, bbId, e),
             static_cast<const AIBlackboard*>(nullptr));
    {
        const auto* loot = user::tryGet<LootRoll>(w, lootId, e);
        CHECK(loot != nullptr);
        CHECK_EQ(loot->seed, std::uint64_t{0xdeadbeefull});
    }

    // Idempotent remove (no-op when bit is absent).
    engine.registerSystem(makeRecorder(
        [e, bbId](Range, CommandBuffer& cb) {
            removeUserComponent(cb, bbId, e);
        }));
    engine.step();
    CHECK(!user::has(w, bbId, e));

    // Round-trip: re-add AIBlackboard with a new value, both bits live.
    engine.registerSystem(makeRecorder(
        [e, bbId](Range, CommandBuffer& cb) {
            addUserComponent<AIBlackboard>(cb, bbId, e,
                AIBlackboard{99, 9.5f, 3u});
        }));
    engine.step();
    CHECK(user::has(w, bbId, e));
    CHECK(user::has(w, lootId, e));
    {
        const auto* bb   = user::tryGet<AIBlackboard>(w, bbId, e);
        const auto* loot = user::tryGet<LootRoll>(w, lootId, e);
        CHECK(bb != nullptr && loot != nullptr);
        CHECK_EQ(bb->stateId, std::int32_t{99});
        CHECK_EQ(bb->cooldown, 9.5f);
        CHECK_EQ(loot->seed, std::uint64_t{0xdeadbeefull});
    }

    // Compose with built-in tag flips: an addTag on Component::StaticTag
    // alongside a user-component add lands in the same tick.
    engine.registerSystem(makeRecorder(
        [e, bbId](Range, CommandBuffer& cb) {
            cb.addTag(e, Component::StaticTag);
            addUserComponent<AIBlackboard>(cb, bbId, e,
                AIBlackboard{1, 0.0f, 0u});
        }));
    engine.step();
    CHECK(w.hasTag(e, Component::StaticTag));
    CHECK(user::has(w, bbId, e));
    {
        const auto* bb = user::tryGet<AIBlackboard>(w, bbId, e);
        CHECK(bb != nullptr);
        CHECK_EQ(bb->stateId, std::int32_t{1});
    }

    // Spawn a second entity AFTER the registry has been used. Its
    // archetype is created on commit with whatever user-bit columns the
    // initial mask carries; verify a follow-up addUserComponent reaches
    // the new entity correctly.
    EntityHandle second{};
    engine.registerSystem(makeRecorder(
        [](Range, CommandBuffer& cb) {
            cb.spawn(Transform{});
        }));
    engine.step();
    // The new entity is the most-recently-spawned with no RenderTag —
    // walk entities, find the one we didn't have before.
    for (auto h : w.entities()) if (h.index != e.index) { second = h; break; }
    CHECK(second.valid());
    CHECK(!user::has(w, lootId, second));

    engine.registerSystem(makeRecorder(
        [second, lootId](Range, CommandBuffer& cb) {
            addUserComponent<LootRoll>(cb, lootId, second, LootRoll{0x42ull});
        }));
    engine.step();
    {
        const auto* loot = user::tryGet<LootRoll>(w, lootId, second);
        CHECK(loot != nullptr);
        CHECK_EQ(loot->seed, std::uint64_t{0x42ull});
        // The original entity's loot must not have changed.
        const auto* orig = user::tryGet<LootRoll>(w, lootId, e);
        CHECK(orig != nullptr);
        CHECK_EQ(orig->seed, std::uint64_t{0xdeadbeefull});
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
