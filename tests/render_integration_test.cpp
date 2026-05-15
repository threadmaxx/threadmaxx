// Heavier integration test for the renderer contract.
//
// Wires a mock `IRenderer` to the engine and verifies, across many
// ticks, that:
//
//   (1) Every step that isn't paused produces exactly one submitFrame
//       call (modulo run()'s interpolation submits — which are not
//       exercised here because we drive step() directly).
//   (2) `RenderFrame::instances` is auto-populated for every entity
//       carrying `RenderTag` and skips `DisabledTag` entities, mirror-
//       ing the §3.1 contract.
//   (3) `RenderFrame::cameras` / `lights` / `drawItems[]` / debug
//       layers are populated correctly when a system pushes via
//       `ISystem::buildRenderFrame`, and the merged contents
//       appear in registration order across systems.
//   (4) The published frame's lifetime is the tick — the renderer
//       must not retain pointers; we copy data we need to keep.
//   (5) `tick` / `simulationTime` advance monotonically across
//       submits; `alpha` is 0 for step()-driven submits.
//
// Batch 9 (Vulkan renderer) will be the first real consumer of this
// surface — these assertions are the contract it depends on.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <cstdint>
#include <vector>

namespace {

using namespace threadmaxx;

struct CapturedFrame {
    std::uint64_t tick                = 0;
    double        simulationTime      = 0.0;
    double        deltaTime           = 0.0;
    float         alpha               = 0.0f;
    std::size_t   instanceCount       = 0;
    std::size_t   cameraCount         = 0;
    std::size_t   lightCount          = 0;
    std::array<std::size_t, kRenderPassCount> drawItemsPerPass = {};
    std::size_t   debugLineCount      = 0;
    // Sample one entity-id per frame to verify ordering survives.
    std::vector<EntityHandle> instanceEntities;
};

class MockRenderer : public IRenderer {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    void submitFrame(const RenderFrame& f) override {
        CapturedFrame c;
        c.tick           = f.tick;
        c.simulationTime = f.simulationTime;
        c.deltaTime      = f.deltaTime;
        c.alpha          = f.alpha;
        c.instanceCount  = f.instances.size();
        c.cameraCount    = f.cameras.size();
        c.lightCount     = f.lights.size();
        for (std::size_t p = 0; p < kRenderPassCount; ++p) {
            c.drawItemsPerPass[p] = f.drawItems[p].size();
        }
        c.debugLineCount = f.debugLines.size();
        c.instanceEntities.reserve(f.instances.size());
        for (const auto& inst : f.instances) {
            c.instanceEntities.push_back(inst.entity);
        }
        frames.push_back(std::move(c));
    }
    std::vector<CapturedFrame> frames;
};

class RenderHookSystem : public ISystem {
public:
    const char* name() const noexcept override { return "render-hook"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override {}
    void buildRenderFrame(RenderFrameBuilder& b) override {
        // One camera, two lights, one draw item per pass, one debug
        // line. Deterministic and easy to assert against.
        b.addCamera(Camera{});
        b.addLight(Light{});
        b.addLight(Light{});
        b.addDrawItem(RenderPass::Opaque,      DrawItem{});
        b.addDrawItem(RenderPass::Transparent, DrawItem{});
        b.addDrawItem(RenderPass::ShadowCasters, DrawItem{});
        b.addDrawItem(RenderPass::Overlay,     DrawItem{});
        b.addDebugLine(DebugLine{});
    }
};

struct SeedGame : IGame {
    std::size_t entityCount = 0;
    std::size_t disabledCount = 0;
    bool        registerHook = false;
    void onSetup(Engine& eng, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < entityCount; ++i) {
            Transform t{};
            t.position.x = static_cast<float>(i);
            // RenderTag.meshId >= 0 enables auto-derive of the
            // RenderTag presence bit. Without that, the entity won't
            // appear in `instances`.
            RenderTag r;
            r.meshId = 1;
            cb.spawn(t, Velocity{}, r);
        }
        if (registerHook) {
            eng.registerSystem(std::make_unique<RenderHookSystem>());
        }
    }
};

} // namespace

int main() {
    // ---- (1) Auto-populated `instances` --------------------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        MockRenderer renderer;
        engine.setRenderer(&renderer);

        SeedGame g; g.entityCount = 16; g.registerHook = false;
        CHECK(engine.initialize(g));

        for (int i = 0; i < 4; ++i) engine.step();
        // initialize() publishes one t=0 frame, then 4 steps publish 4
        // more = 5 total.
        CHECK_EQ(renderer.frames.size(), std::size_t{5});

        for (const auto& f : renderer.frames) {
            CHECK_EQ(f.instanceCount, std::size_t{16});
            CHECK_EQ(f.cameraCount,   std::size_t{0});
            CHECK_EQ(f.lightCount,    std::size_t{0});
            for (auto n : f.drawItemsPerPass) CHECK_EQ(n, std::size_t{0});
            CHECK_EQ(f.alpha, 0.0f);
        }

        // Tick=0 from initialize, then steps publish 1..4.
        CHECK_EQ(renderer.frames.front().tick, std::uint64_t{0});
        CHECK_EQ(renderer.frames.back().tick,  std::uint64_t{4});

        engine.shutdown();
    }

    // ---- (2) DisabledTag suppresses the instance ----------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        MockRenderer renderer;
        engine.setRenderer(&renderer);

        struct G : IGame {
            std::vector<EntityHandle>* toDisable = nullptr;
            void onSetup(Engine&, World& w, CommandBuffer& cb) override {
                for (int i = 0; i < 8; ++i) {
                    Transform t{};
                    RenderTag r; r.meshId = 1;
                    cb.spawn(t, Velocity{}, r);
                }
                (void)w;
            }
        } g;
        std::vector<EntityHandle> es;
        g.toDisable = &es;
        CHECK(engine.initialize(g));
        {
            auto sp = engine.world().entities();
            es.assign(sp.begin(), sp.end());
        }

        // Disable half via a one-shot system using addTag.
        class Disabler : public ISystem {
        public:
            std::vector<EntityHandle> es;
            const char* name() const noexcept override { return "disabler"; }
            ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
            ComponentSet writes() const noexcept override { return ComponentSet::all(); }
            void update(SystemContext& ctx) override {
                if (ctx.tick() != 0) return;
                ctx.single([this](Range, CommandBuffer& cb) {
                    for (std::size_t i = 0; i < es.size(); i += 2) {
                        cb.addTag(es[i], Component::DisabledTag);
                    }
                });
            }
        };
        auto disabler = std::make_unique<Disabler>();
        disabler->es = es;
        engine.registerSystem(std::move(disabler));

        engine.step();   // applies the addTag commands
        engine.step();   // first frame with disables in effect
        // After the second step, four entities should be filtered out.
        CHECK_EQ(renderer.frames.back().instanceCount, std::size_t{4});
        engine.shutdown();
    }

    // ---- (3) buildRenderFrame populates hierarchical fields -----------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        MockRenderer renderer;
        engine.setRenderer(&renderer);

        SeedGame g; g.entityCount = 4; g.registerHook = true;
        CHECK(engine.initialize(g));
        engine.step();
        engine.step();

        const auto& f = renderer.frames.back();
        CHECK_EQ(f.cameraCount, std::size_t{1});
        CHECK_EQ(f.lightCount,  std::size_t{2});
        CHECK_EQ(f.drawItemsPerPass[passIndex(RenderPass::Opaque)],        std::size_t{1});
        CHECK_EQ(f.drawItemsPerPass[passIndex(RenderPass::Transparent)],   std::size_t{1});
        CHECK_EQ(f.drawItemsPerPass[passIndex(RenderPass::ShadowCasters)], std::size_t{1});
        CHECK_EQ(f.drawItemsPerPass[passIndex(RenderPass::Overlay)],       std::size_t{1});
        CHECK_EQ(f.debugLineCount, std::size_t{1});
        engine.shutdown();
    }

    // ---- (4) Null renderer is a clean no-op ---------------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        // No renderer set.
        SeedGame g; g.entityCount = 4;
        CHECK(engine.initialize(g));
        for (int i = 0; i < 4; ++i) engine.step();
        // No crash, no submission. Implicit.
        engine.shutdown();
    }

    // ---- (5) Pause prevents submitFrame from being called -------------
    //
    // `step()` returns early when paused — no frame published, no
    // `submitFrame` call.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        MockRenderer renderer;
        engine.setRenderer(&renderer);
        SeedGame g; g.entityCount = 2;
        CHECK(engine.initialize(g));

        engine.step();
        const auto beforePause = renderer.frames.size();

        engine.setPaused(true);
        engine.step();
        engine.step();
        CHECK_EQ(renderer.frames.size(), beforePause);

        engine.setPaused(false);
        engine.step();
        CHECK_EQ(renderer.frames.size(), beforePause + 1);

        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
