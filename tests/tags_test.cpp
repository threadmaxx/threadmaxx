// §3.1 batch 5: tag-only components (StaticTag, DisabledTag,
// DestroyedTag). They have no dense storage — presence is the bit
// alone. Verify:
//   - cb.addTag / cb.removeTag flip exactly that bit.
//   - World::hasTag observes the result.
//   - A DisabledTag entity is excluded from the rendered RenderFrame
//     (the engine's only built-in consumer of a tag bit).
//   - Tag bits round-trip through serialize/deserialize.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <sstream>

namespace {

class SeedingGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        // Two renderable entities. We'll disable one mid-test.
        threadmaxx::RenderTag tag;
        tag.meshId = 0;
        seed.spawn(threadmaxx::Transform{}, {}, tag);
        tag.meshId = 1;
        seed.spawn(threadmaxx::Transform{}, {}, tag);
    }
};

class TagFlipper : public threadmaxx::ISystem {
public:
    bool done = false;
    const char* name() const noexcept override { return "tagflip"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        const auto entities = ctx.world().entities();
        if (entities.size() < 2) return;
        auto e0 = entities[0];
        auto e1 = entities[1];
        ctx.single([e0, e1]
            (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
                cb.addTag(e0, threadmaxx::Component::StaticTag);
                cb.addTag(e1, threadmaxx::Component::DisabledTag);
            });
    }
};

class Capturer : public threadmaxx::IRenderer {
public:
    std::size_t lastInstances = 0;
    bool initialize() override { return true; }
    void submitFrame(const threadmaxx::RenderFrame& f) override {
        lastInstances = f.instances.size();
    }
    void shutdown() override {}
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    SeedingGame game;
    Capturer cap;
    engine.setRenderer(&cap);
    CHECK(engine.initialize(game));
    engine.registerSystem(std::make_unique<TagFlipper>());

    // Pre-flip: two renderables in the frame.
    engine.step();
    // First step: the flipper queues add/remove. After commit + render,
    // entity 1 has DisabledTag and is dropped from the render list.
    CHECK_EQ(cap.lastInstances, std::size_t{1});

    const auto& w = engine.world();
    const auto e0 = w.entities()[0];
    const auto e1 = w.entities()[1];
    CHECK(w.hasTag(e0, Component::StaticTag));
    CHECK(!w.hasTag(e0, Component::DisabledTag));
    CHECK(w.hasTag(e1, Component::DisabledTag));
    CHECK(!w.hasTag(e1, Component::StaticTag));

    // Verify removeTag clears the bit by toggling e1 back on.
    {
        class Once : public ISystem {
        public:
            EntityHandle target;
            bool done = false;
            const char* name() const noexcept override { return "untoggle"; }
            void update(SystemContext& ctx) override {
                if (done) return;
                done = true;
                auto t = target;
                ctx.single([t](Range, CommandBuffer& cb) {
                    cb.removeTag(t, Component::DisabledTag);
                });
            }
        };
        auto u = std::make_unique<Once>();
        u->target = e1;
        engine.registerSystem(std::move(u));
    }
    engine.step();
    CHECK(!w.hasTag(e1, Component::DisabledTag));
    CHECK_EQ(cap.lastInstances, std::size_t{2});

    // Snapshot preserves tag bits.
    const auto snap = w.snapshot();
    CHECK(snap.masks[0].has(Component::StaticTag));

    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    serialize(ss, snap);
    WorldSnapshot back;
    CHECK(deserialize(ss, back));
    CHECK(back.masks[0].has(Component::StaticTag));
    // The roundtripped mask should NOT carry DisabledTag (we cleared
    // it above before snapshotting).
    CHECK(!back.masks[1].has(Component::DisabledTag));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
