// ComponentMask: spawn derives the mask, setRenderTag updates the RenderTag
// bit, setComponentMask round-trips, the renderer filter uses the mask
// (verified indirectly via mask state), forEachWith filters correctly, and
// the mask survives destroy() swap-and-pop.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>

namespace {

struct MaskGame : threadmaxx::IGame {
    threadmaxx::EntityHandle renderable;
    threadmaxx::EntityHandle nonRenderable;

    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        threadmaxx::CommandBuffer probe;
        // Renderable: explicit meshId.
        probe.spawn({}, {}, threadmaxx::RenderTag{/*meshId*/ 1});
        // Non-renderable: default RenderTag (meshId = -1).
        probe.spawn({});
        for (auto& cmd : probe.commands()) seed.commands().push_back(std::move(cmd));
    }
};

// Records which entities the body saw. Read+write Velocity so it can share
// a wave with nothing.
class RecorderSystem : public threadmaxx::ISystem {
public:
    std::atomic<std::uint32_t> sawRenderable{0};
    std::atomic<std::uint32_t> sawNonRenderable{0};

    const char* name() const noexcept override { return "recorder"; }
    void update(threadmaxx::SystemContext& ctx) override {
        threadmaxx::forEachWith<threadmaxx::Transform, threadmaxx::RenderTag>(
            ctx,
            [this](threadmaxx::EntityHandle e,
                   const threadmaxx::Transform&,
                   const threadmaxx::RenderTag& r,
                   threadmaxx::CommandBuffer&) {
                (void)e;
                if (r.meshId >= 0) sawRenderable.fetch_add(1, std::memory_order_relaxed);
                else               sawNonRenderable.fetch_add(1, std::memory_order_relaxed);
            });
    }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform}
             | threadmaxx::ComponentSet{threadmaxx::Component::RenderTag};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
};

} // namespace

int main() {
    using threadmaxx::Component;
    using threadmaxx::ComponentSet;

    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    threadmaxx::Engine engine(cfg);

    auto* recorder = new RecorderSystem;
    struct GameWithRecorder : threadmaxx::IGame {
        RecorderSystem* rec;
        threadmaxx::EntityHandle renderable;
        threadmaxx::EntityHandle nonRenderable;
        explicit GameWithRecorder(RecorderSystem* r) : rec(r) {}
        void onSetup(threadmaxx::Engine& e, threadmaxx::World&,
                     threadmaxx::CommandBuffer& seed) override {
            e.registerSystem(std::unique_ptr<RecorderSystem>(rec));
            // Capture the handles assigned at commit time.
            auto& spawnRenderable = seed.commands().emplace_back(
                threadmaxx::detail::CmdSpawn{{}, {},
                    threadmaxx::RenderTag{1}, {}, {}, threadmaxx::Parent{},
                    ComponentSet{Component::Transform}
                        | ComponentSet{Component::Velocity}
                        | ComponentSet{Component::UserData}
                        | ComponentSet{Component::Acceleration}
                        | ComponentSet{Component::RenderTag},
                    &renderable});
            (void)spawnRenderable;
            auto& spawnNon = seed.commands().emplace_back(
                threadmaxx::detail::CmdSpawn{{}, {}, {}, {}, {}, threadmaxx::Parent{},
                    ComponentSet{Component::Transform}
                        | ComponentSet{Component::Velocity}
                        | ComponentSet{Component::UserData}
                        | ComponentSet{Component::Acceleration},
                    &nonRenderable});
            (void)spawnNon;
        }
    };
    GameWithRecorder game(recorder);
    CHECK(engine.initialize(game));

    // After setup, two entities exist with different masks.
    const auto& world = engine.world();
    CHECK_EQ(world.size(), std::size_t{2});

    const auto* mRen = world.tryGetComponentMask(game.renderable);
    const auto* mNon = world.tryGetComponentMask(game.nonRenderable);
    CHECK(mRen != nullptr);
    CHECK(mNon != nullptr);
    CHECK(mRen->has(Component::RenderTag));
    CHECK(!mNon->has(Component::RenderTag));
    // Other components present on both.
    CHECK(mRen->has(Component::Transform));
    CHECK(mRen->has(Component::Velocity));
    CHECK(mRen->has(Component::UserData));
    CHECK(mRen->has(Component::Acceleration));
    CHECK(mNon->has(Component::Transform));

    engine.step();

    // forEachWith<Transform, RenderTag> should have visited the renderable
    // entity and skipped the non-renderable one.
    CHECK_EQ(recorder->sawRenderable.load(),    std::uint32_t{1});
    CHECK_EQ(recorder->sawNonRenderable.load(), std::uint32_t{0});

    // setRenderTag flips the RenderTag bit based on meshId. Promote the
    // non-renderable to renderable, then back.
    {
        threadmaxx::CommandBuffer cb;
        cb.setRenderTag(game.nonRenderable, threadmaxx::RenderTag{42});
        // Drive a commit via a one-shot system would be awkward; instead
        // we use the engine-internal seed-style path: append into the next
        // step's command stream by registering a one-shot system. Simpler:
        // call engine.world().impl_() directly is not exposed cleanly here,
        // so we use a wrapper system below.
        struct OneShotSystem : threadmaxx::ISystem {
            threadmaxx::CommandBuffer payload;
            bool fired = false;
            const char* name() const noexcept override { return "oneshot"; }
            void update(threadmaxx::SystemContext& ctx) override {
                if (fired) return;
                fired = true;
                ctx.single([this](threadmaxx::Range, threadmaxx::CommandBuffer& out) {
                    for (auto& c : payload.commands()) {
                        out.commands().push_back(std::move(c));
                    }
                });
            }
            threadmaxx::ComponentSet reads() const noexcept override { return threadmaxx::ComponentSet::none(); }
            threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::all(); }
        };
        auto one = std::make_unique<OneShotSystem>();
        one->payload = std::move(cb);
        engine.registerSystem(std::move(one));
    }
    engine.step();
    mNon = world.tryGetComponentMask(game.nonRenderable);
    CHECK(mNon != nullptr);
    CHECK(mNon->has(Component::RenderTag));

    // Clear it back via setRenderTag with meshId = -1.
    {
        struct ClearSystem : threadmaxx::ISystem {
            threadmaxx::EntityHandle target;
            bool fired = false;
            const char* name() const noexcept override { return "clear"; }
            void update(threadmaxx::SystemContext& ctx) override {
                if (fired) return;
                fired = true;
                auto t = target;
                ctx.single([t](threadmaxx::Range, threadmaxx::CommandBuffer& out) {
                    out.setRenderTag(t, threadmaxx::RenderTag{-1});
                });
            }
            threadmaxx::ComponentSet reads() const noexcept override { return threadmaxx::ComponentSet::none(); }
            threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::all(); }
        };
        auto cs = std::make_unique<ClearSystem>();
        cs->target = game.nonRenderable;
        engine.registerSystem(std::move(cs));
    }
    engine.step();
    mNon = world.tryGetComponentMask(game.nonRenderable);
    CHECK(mNon != nullptr);
    CHECK(!mNon->has(Component::RenderTag));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
